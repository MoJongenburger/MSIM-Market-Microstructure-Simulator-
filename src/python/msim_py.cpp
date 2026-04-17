// ============================================================
// src/python/msim_py.cpp
// ============================================================

// Must come before any include that pulls in <cmath> on MSVC,
// otherwise M_PI is undefined (Windows does not expose it by default).
#define _USE_MATH_DEFINES

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/operators.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

// Core MSIM headers
#include "msim/types.hpp"
#include "msim/order.hpp"
#include "msim/matching_engine.hpp"
#include "msim/world.hpp"
#include "msim/latency_model.hpp"
#include "msim/stylized_facts.hpp"
#include "msim/hawkes_process.hpp"
#include "msim/shared_fundamental.hpp"

// Built-in agents
#include "msim/agents/fundamental_value_agent.hpp"
#include "msim/agents/momentum_agent.hpp"
#include "msim/agents/noise_trader_hawkes.hpp"
#include "msim/agents/market_maker_as.hpp"
#include "msim/agents/multi_asset_fv_agent.hpp"
#include "msim/agents/vwap_agent.hpp"
#include "msim/agents/twap_agent.hpp"
#include "msim/agents/is_agent.hpp"

namespace py = pybind11;
using namespace msim;
using namespace msim::agents;

// ─── Trampoline ───────────────────────────────────────────────────────────────
class PyAgent : public IAgent {
public:
    using IAgent::IAgent;

    OwnerId owner() const noexcept override {
        PYBIND11_OVERRIDE_PURE(OwnerId, IAgent, owner);
    }

    void seed(uint64_t s) override {
        PYBIND11_OVERRIDE_PURE(void, IAgent, seed, s);
    }

    void step(Ts ts,
              const MarketView&    view,
              const AgentState&    self_state,
              std::vector<Action>& out) override
    {
        py::gil_scoped_acquire acquire;
        py::function overridden = py::get_override(this, "step");
        if (!overridden) return;
        py::object ret = overridden(ts, view, self_state);
        if (ret.is_none()) return;
        for (auto item : ret)
            out.push_back(item.cast<Action>());
    }
};

static py::object opt_to_py(const std::optional<Price>& opt) {
    if (opt) return py::int_(*opt);
    return py::none();
}

PYBIND11_MODULE(_msim_core, m) {
    m.doc() = R"pbdoc(
        MSIM — Market Microstructure Simulator
        ======================================
        Sub-100ns C++20 matching engine with Python strategy interface.

        Quick start
        -----------
        >>> import msim
        >>> world = msim.World()
        >>> world.prefill_book(mid=10000, levels=20, qty=10)
        >>> world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
        >>> result = world.run(seed=42, horizon=1.0)
        >>> df = result.trades_df()
    )pbdoc";

    // ── Enums ────────────────────────────────────────────────────────────────

    py::enum_<Side>(m, "Side", "Order side")
        .value("Buy",  Side::Buy)
        .value("Sell", Side::Sell)
        .export_values();

    py::enum_<OrderType>(m, "OrderType", "Order type")
        .value("Limit",  OrderType::Limit)
        .value("Market", OrderType::Market)
        .export_values();

    py::enum_<TimeInForce>(m, "TimeInForce", "Time-in-force instruction")
        .value("GTC", TimeInForce::GTC)
        .value("IOC", TimeInForce::IOC)
        .value("FOK", TimeInForce::FOK)
        .export_values();

    py::enum_<MarketStyle>(m, "MarketStyle", "Market order style")
        .value("PureMarket",    MarketStyle::PureMarket)
        .value("MarketToLimit", MarketStyle::MarketToLimit)
        .export_values();

    py::enum_<ActionType>(m, "ActionType", "World action type")
        .value("Submit",    ActionType::Submit)
        .value("Cancel",    ActionType::Cancel)
        .value("ModifyQty", ActionType::ModifyQty)
        .export_values();

    py::enum_<VWAPSchedule>(m, "VWAPSchedule", "VWAP execution schedule type.")
        .value("FLAT",    VWAPSchedule::FLAT)
        .value("U_SHAPE", VWAPSchedule::U_SHAPE)
        .value("CUSTOM",  VWAPSchedule::CUSTOM)
        .export_values();

    // ── Order ────────────────────────────────────────────────────────────────

    py::class_<Order>(m, "Order", R"pbdoc(
        A single order submitted to the matching engine.
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("id",        &Order::id)
        .def_readwrite("owner",     &Order::owner)
        .def_readwrite("side",      &Order::side)
        .def_readwrite("type",      &Order::type)
        .def_readwrite("price",     &Order::price)
        .def_readwrite("qty",       &Order::qty)
        .def_readwrite("ts",        &Order::ts)
        .def_readwrite("tif",       &Order::tif)
        .def_readwrite("mkt_style", &Order::mkt_style)
        .def("__repr__", [](const Order& o) {
            return "<Order id=" + std::to_string(o.id)
                 + " side=" + (o.side == Side::Buy ? "Buy" : "Sell")
                 + " qty=" + std::to_string(o.qty)
                 + " px=" + std::to_string(o.price) + ">";
        });

    // ── Trade ────────────────────────────────────────────────────────────────

    py::class_<Trade>(m, "Trade")
        .def(py::init<>())
        .def_readwrite("id",             &Trade::id)
        .def_readwrite("ts",             &Trade::ts)
        .def_readwrite("price",          &Trade::price)
        .def_readwrite("qty",            &Trade::qty)
        .def_readwrite("maker_order_id", &Trade::maker_order_id)
        .def_readwrite("taker_order_id", &Trade::taker_order_id)
.def_readwrite("aggressor_side", &Trade::aggressor_side)
.def("__repr__", [](const Trade& t) {
            return "<Trade ts=" + std::to_string(t.ts)
                 + " px=" + std::to_string(t.price)
                 + " qty=" + std::to_string(t.qty) + ">";
        });

    // ── BookTop ──────────────────────────────────────────────────────────────

    py::class_<BookTop>(m, "BookTop")
        .def(py::init<>())
        .def_readwrite("ts", &BookTop::ts)
        .def_property_readonly("best_bid",
            [](const BookTop& b) { return opt_to_py(b.best_bid); })
        .def_property_readonly("best_ask",
            [](const BookTop& b) { return opt_to_py(b.best_ask); })
        .def_property_readonly("mid",
            [](const BookTop& b) { return opt_to_py(b.mid); });

    // ── LevelSummary ─────────────────────────────────────────────────────────

    py::class_<LevelSummary>(m, "LevelSummary")
        .def(py::init<>())
        .def_readwrite("price",       &LevelSummary::price)
        .def_readwrite("total_qty",   &LevelSummary::total_qty)
        .def_readwrite("order_count", &LevelSummary::order_count);

    // ── MarketView ───────────────────────────────────────────────────────────

    py::class_<MarketView>(m, "MarketView")
        .def(py::init<>())
        .def_readwrite("ts",         &MarketView::ts)
        .def_property_readonly("best_bid",
            [](const MarketView& v) { return opt_to_py(v.best_bid); })
        .def_property_readonly("best_ask",
            [](const MarketView& v) { return opt_to_py(v.best_ask); })
        .def_property_readonly("mid",
            [](const MarketView& v) { return opt_to_py(v.mid); })
        .def_property_readonly("last_trade",
            [](const MarketView& v) { return opt_to_py(v.last_trade); })
        .def_readwrite("bid_depth",  &MarketView::bid_depth)
        .def_readwrite("ask_depth",  &MarketView::ask_depth)
        .def_readwrite("imbalance",  &MarketView::imbalance)
        .def("has_quote", [](const MarketView& v) {
            return v.best_bid.has_value() && v.best_ask.has_value();
        })
        .def("spread", [](const MarketView& v) -> py::object {
            if (v.best_bid && v.best_ask)
                return py::int_(*v.best_ask - *v.best_bid);
            return py::none();
        });

    // ── QueuePosition ────────────────────────────────────────────────────────

    py::class_<QueuePosition>(m, "QueuePosition")
        .def_readwrite("order_id",       &QueuePosition::order_id)
        .def_readwrite("price",          &QueuePosition::price)
        .def_readwrite("side",           &QueuePosition::side)
        .def_readwrite("qty_ahead",      &QueuePosition::qty_ahead)
        .def_readwrite("qty_behind",     &QueuePosition::qty_behind)
        .def_readwrite("level_total",    &QueuePosition::level_total)
        .def_readwrite("own_qty",        &QueuePosition::own_qty)
        .def_readwrite("position_index", &QueuePosition::position_index)
        .def("is_front", [](const QueuePosition& qp) {
            return qp.qty_ahead == 0;
        })
        .def("queue_fraction", [](const QueuePosition& qp) -> double {
            if (qp.level_total == 0) return 0.0;
            return static_cast<double>(qp.qty_ahead)
                 / static_cast<double>(qp.level_total);
        });

    // ── AgentState ───────────────────────────────────────────────────────────

    py::class_<AgentState>(m, "AgentState")
        .def(py::init<>())
        .def_readwrite("owner",           &AgentState::owner)
        .def_readwrite("cash_ticks",      &AgentState::cash_ticks)
        .def_readwrite("position",        &AgentState::position)
        .def_readwrite("queue_positions", &AgentState::queue_positions)
        .def("has_resting_orders", [](const AgentState& s) {
            return !s.queue_positions.empty();
        })
        .def("resting_bid", [](const AgentState& s) -> py::object {
            for (const auto& qp : s.queue_positions)
                if (qp.side == Side::Buy) return py::cast(qp);
            return py::none();
        })
        .def("resting_ask", [](const AgentState& s) -> py::object {
            for (const auto& qp : s.queue_positions)
                if (qp.side == Side::Sell) return py::cast(qp);
            return py::none();
        });

    // ── Action ───────────────────────────────────────────────────────────────

    py::class_<Action>(m, "Action")
        .def(py::init<>())
        .def_readwrite("type",    &Action::type)
        .def_readwrite("order",   &Action::order)
        .def_readwrite("id",      &Action::id)
        .def_readwrite("new_qty", &Action::new_qty)
        .def_static("submit",     &Action::submit,     py::arg("order"))
        .def_static("cancel",     &Action::cancel,     py::arg("order_id"))
        .def_static("modify_qty", &Action::modify_qty, py::arg("order_id"),
                    py::arg("new_qty"));

    // ── IAgent ───────────────────────────────────────────────────────────────

    py::class_<IAgent, PyAgent, std::unique_ptr<IAgent>>(m, "Agent")
        .def(py::init<>())
        .def("owner", &IAgent::owner)
        .def("seed",  &IAgent::seed, py::arg("s"))
        .def("step",  [](IAgent&, Ts, const MarketView&,
                         const AgentState&) -> py::list {
                return py::list();
             }, py::arg("ts"), py::arg("view"), py::arg("state"));

    // ── FVLogEntry ───────────────────────────────────────────────────────────

    py::class_<FVLogEntry>(m, "FVLogEntry")
        .def_readwrite("ts",    &FVLogEntry::ts)
        .def_readwrite("owner", &FVLogEntry::owner)
        .def_readwrite("V",     &FVLogEntry::V);

    // ── Stylized facts ────────────────────────────────────────────────────────

    py::class_<ReturnStats>(m, "ReturnStats")
        .def_readwrite("mean",             &ReturnStats::mean)
        .def_readwrite("variance",         &ReturnStats::variance)
        .def_readwrite("std_dev",          &ReturnStats::std_dev)
        .def_readwrite("skewness",         &ReturnStats::skewness)
        .def_readwrite("excess_kurtosis",  &ReturnStats::excess_kurtosis)
        .def_readwrite("min_return",       &ReturnStats::min_return)
        .def_readwrite("max_return",       &ReturnStats::max_return)
        .def_readwrite("n_obs",            &ReturnStats::n_obs);

    py::class_<AutocorrResult>(m, "AutocorrResult")
        .def_readwrite("return_ac",      &AutocorrResult::return_ac)
        .def_readwrite("abs_return_ac",  &AutocorrResult::abs_return_ac)
        .def_readwrite("sign_flow_ac",   &AutocorrResult::sign_flow_ac)
        .def_readwrite("max_lag",        &AutocorrResult::max_lag);

    py::class_<PriceImpactResult>(m, "PriceImpactResult")
        .def_readwrite("kyle_lambda",     &PriceImpactResult::kyle_lambda)
        .def_readwrite("r_squared",       &PriceImpactResult::r_squared)
        .def_readwrite("power_exponent",  &PriceImpactResult::power_exponent)
        .def_readwrite("bin_midpoints",   &PriceImpactResult::bin_midpoints)
        .def_readwrite("bin_impact",      &PriceImpactResult::bin_impact);

    py::class_<SpreadStats>(m, "SpreadStats")
        .def_readwrite("time_weighted_spread",   &SpreadStats::time_weighted_spread)
        .def_readwrite("realized_spread_mean",   &SpreadStats::realized_spread_mean)
        .def_readwrite("adverse_selection_mean", &SpreadStats::adverse_selection_mean)
        .def_readwrite("effective_spread_mean",  &SpreadStats::effective_spread_mean);

    py::class_<AmihudStats>(m, "AmihudStats")
        .def_readwrite("mean_illiq",   &AmihudStats::mean_illiq)
        .def_readwrite("std_illiq",    &AmihudStats::std_illiq)
        .def_readwrite("illiq_series", &AmihudStats::illiq_series);

    py::class_<StyleFacts>(m, "StyleFacts")
        .def_readwrite("returns",           &StyleFacts::returns)
        .def_readwrite("autocorr",          &StyleFacts::autocorr)
        .def_readwrite("impact",            &StyleFacts::impact)
        .def_readwrite("spreads",           &StyleFacts::spreads)
        .def_readwrite("amihud",            &StyleFacts::amihud)
        .def_readwrite("fat_tails_ok",      &StyleFacts::fat_tails_ok)
        .def_readwrite("vol_clustering_ok", &StyleFacts::vol_clustering_ok)
        .def_readwrite("flow_autocorr_ok",  &StyleFacts::flow_autocorr_ok)
        .def_readwrite("positive_spread_ok",&StyleFacts::positive_spread_ok)
        .def_readwrite("positive_impact_ok",&StyleFacts::positive_impact_ok)
        .def("summary", &StylizedFactsMeasurer::summary)
        .def("passes", [](const StyleFacts& sf) {
            return sf.fat_tails_ok && sf.vol_clustering_ok
                && sf.flow_autocorr_ok && sf.positive_spread_ok
                && sf.positive_impact_ok;
        });

    // ── AccountSnapshot ───────────────────────────────────────────────────────

    py::class_<AccountSnapshot>(m, "AccountSnapshot")
        .def_readwrite("owner",      &AccountSnapshot::owner)
        .def_readwrite("cash_ticks", &AccountSnapshot::cash_ticks)
        .def_readwrite("position",   &AccountSnapshot::position);

    // ── TCA types ─────────────────────────────────────────────────────────────

    py::class_<ArrivalInfo>(m, "ArrivalInfo")
        .def_readwrite("arrival_mid", &ArrivalInfo::arrival_mid)
        .def_readwrite("is_limit",    &ArrivalInfo::is_limit);

    py::class_<FillRecord>(m, "FillRecord")
        .def_readwrite("ts",          &FillRecord::ts)
        .def_readwrite("owner",       &FillRecord::owner)
        .def_readwrite("order_id",    &FillRecord::order_id)
        .def_readwrite("side",        &FillRecord::side)
        .def_readwrite("fill_qty",    &FillRecord::fill_qty)
        .def_readwrite("fill_price",  &FillRecord::fill_price)
        .def_readwrite("arrival_mid", &FillRecord::arrival_mid)
        .def_readwrite("is_maker",    &FillRecord::is_maker)
        .def("slippage_ticks", &FillRecord::slippage_ticks);

    py::class_<StepSnapshot>(m, "StepSnapshot")
        .def_readwrite("ts",         &StepSnapshot::ts)
        .def_readwrite("owner",      &StepSnapshot::owner)
        .def_readwrite("position",   &StepSnapshot::position)
        .def_readwrite("cash_ticks", &StepSnapshot::cash_ticks)
        .def_readwrite("mid",        &StepSnapshot::mid)
        .def("mtm_pnl", &StepSnapshot::mtm_pnl);

    py::class_<AgentTCA>(m, "AgentTCA")
        .def_readwrite("owner",                   &AgentTCA::owner)
        .def_readwrite("n_orders_submitted",      &AgentTCA::n_orders_submitted)
        .def_readwrite("n_limit_submitted",       &AgentTCA::n_limit_submitted)
        .def_readwrite("n_market_submitted",      &AgentTCA::n_market_submitted)
        .def_readwrite("n_cancels_sent",          &AgentTCA::n_cancels_sent)
        .def_readwrite("n_fills_maker",           &AgentTCA::n_fills_maker)
        .def_readwrite("n_fills_taker",           &AgentTCA::n_fills_taker)
        .def_readwrite("total_qty_maker",         &AgentTCA::total_qty_maker)
        .def_readwrite("total_qty_taker",         &AgentTCA::total_qty_taker)
        .def_readwrite("total_qty_traded",        &AgentTCA::total_qty_traded)
        .def_readwrite("limit_fill_rate",         &AgentTCA::limit_fill_rate)
        .def_readwrite("avg_slippage_ticks",      &AgentTCA::avg_slippage_ticks)
        .def_readwrite("total_slippage_ticks",    &AgentTCA::total_slippage_ticks)
        .def_readwrite("turnover_notional_ticks", &AgentTCA::turnover_notional_ticks)
        .def_readwrite("final_position",          &AgentTCA::final_position)
        .def_readwrite("final_cash_ticks",        &AgentTCA::final_cash_ticks)
        .def_readwrite("final_mtm_pnl",           &AgentTCA::final_mtm_pnl);

    // ── WorldResult ──────────────────────────────────────────────────────────

    py::class_<WorldResult>(m, "WorldResult")
        .def_readwrite("trades",          &WorldResult::trades)
        .def_readwrite("tops",            &WorldResult::tops)
        .def_readwrite("accounts",        &WorldResult::accounts)
        .def_readwrite("cancel_failures", &WorldResult::cancel_failures)
        .def_readwrite("modify_failures", &WorldResult::modify_failures)
        .def_readwrite("fv_log",          &WorldResult::fv_log)
        .def_readwrite("fills",           &WorldResult::fills)
        .def_readwrite("pnl_series",      &WorldResult::pnl_series)
        .def_readwrite("tca",             &WorldResult::tca)
        .def_property_readonly("sf", [](const WorldResult& r) -> py::object {
            if (r.sf) return py::cast(*r.sf);
            return py::none();
        })
        .def("trades_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& t : r.trades) {
                py::dict row;
                row["id"]             = t.id;
                row["ts"]             = t.ts;
                row["price"]          = t.price;
                row["qty"]            = t.qty;
                row["maker_order_id"] = t.maker_order_id;
                row["taker_order_id"] = t.taker_order_id;
                row["aggressor_side"] = (t.aggressor_side == Side::Buy ? "Buy" : "Sell");
            rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("tops_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& t : r.tops) {
                py::dict row;
                row["ts"]       = t.ts;
                row["best_bid"] = opt_to_py(t.best_bid);
                row["best_ask"] = opt_to_py(t.best_ask);
                row["mid"]      = opt_to_py(t.mid);
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("accounts_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& a : r.accounts) {
                py::dict row;
                row["owner"]      = a.owner;
                row["cash_ticks"] = a.cash_ticks;
                row["position"]   = a.position;
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("fv_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& e : r.fv_log) {
                py::dict row;
                row["ts"]    = e.ts;
                row["owner"] = e.owner;
                row["V"]     = e.V;
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("fills_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& f : r.fills) {
                py::dict row;
                row["ts"]          = f.ts;
                row["owner"]       = f.owner;
                row["order_id"]    = f.order_id;
                row["side"]        = (f.side == Side::Buy ? "Buy" : "Sell");
                row["fill_qty"]    = f.fill_qty;
                row["fill_price"]  = f.fill_price;
                row["arrival_mid"] = f.arrival_mid;
                row["is_maker"]    = f.is_maker;
                row["slippage"]    = f.slippage_ticks();
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("pnl_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& s : r.pnl_series) {
                py::dict row;
                row["ts"]         = s.ts;
                row["owner"]      = s.owner;
                row["position"]   = s.position;
                row["cash_ticks"] = s.cash_ticks;
                row["mid"]        = s.mid;
                row["mtm_pnl"]    = s.mtm_pnl();
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("tca_df", [](const WorldResult& r) {
            py::module_ pd = py::module_::import("pandas");
            py::list rows;
            for (const auto& t : r.tca) {
                py::dict row;
                row["owner"]                   = t.owner;
                row["n_orders_submitted"]      = t.n_orders_submitted;
                row["n_limit_submitted"]       = t.n_limit_submitted;
                row["n_market_submitted"]      = t.n_market_submitted;
                row["n_cancels_sent"]          = t.n_cancels_sent;
                row["n_fills_maker"]           = t.n_fills_maker;
                row["n_fills_taker"]           = t.n_fills_taker;
                row["total_qty_traded"]        = t.total_qty_traded;
                row["limit_fill_rate"]         = t.limit_fill_rate;
                row["avg_slippage_ticks"]      = t.avg_slippage_ticks;
                row["turnover_notional_ticks"] = t.turnover_notional_ticks;
                row["final_position"]          = t.final_position;
                row["final_cash_ticks"]        = t.final_cash_ticks;
                row["final_mtm_pnl"]           = t.final_mtm_pnl;
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        })
        .def("summary", [](const WorldResult& r) {
            if (!r.sf) {
                py::print("No stylized facts (set compute_stylized_facts=True).");
                return;
            }
            py::print(StylizedFactsMeasurer::summary(*r.sf));
        });

    // ── WorldConfig ───────────────────────────────────────────────────────────
    // NOTE: CapacityHints was merged into WorldConfig as direct fields
    // (expected_resting_orders, expected_fills) during the performance
    // optimisation pass. The separate CapacityHints struct no longer exists.

    py::class_<WorldConfig>(m, "WorldConfig", R"pbdoc(
        Configuration for a World.run() call.

        Attributes
        ----------
        dt_ns                    : int   — step size in nanoseconds (default 1 ms)
        latency_enabled          : bool
        latency_configs          : list[LatencyDistConfig]
        compute_stylized_facts   : bool  (default True)
        record_fv_signals        : bool  (default False)
        record_fills             : bool  (default True)
        record_pnl_series        : bool  (default True)
        track_queue_positions    : bool  (default True)
        expected_resting_orders  : int   — pre-reserve hint (0 = auto)
        expected_fills           : int   — pre-reserve hint (0 = auto)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("dt_ns",                   &WorldConfig::dt_ns)
        .def_readwrite("latency_enabled",         &WorldConfig::latency_enabled)
        .def_readwrite("latency_configs",         &WorldConfig::latency_configs)
        .def_readwrite("compute_stylized_facts",  &WorldConfig::compute_stylized_facts)
        .def_readwrite("record_fv_signals",       &WorldConfig::record_fv_signals)
        .def_readwrite("record_fills",            &WorldConfig::record_fills)
        .def_readwrite("record_pnl_series",       &WorldConfig::record_pnl_series)
        .def_readwrite("track_queue_positions",   &WorldConfig::track_queue_positions)
        .def_readwrite("expected_resting_orders", &WorldConfig::expected_resting_orders)
        .def_readwrite("expected_fills",          &WorldConfig::expected_fills);

    // ── LatencyDistConfig ─────────────────────────────────────────────────────

    py::enum_<LatencyDistType>(m, "LatencyDistType")
        .value("FIXED",      LatencyDistType::FIXED)
        .value("GAUSSIAN",   LatencyDistType::GAUSSIAN)
        .value("LOG_NORMAL", LatencyDistType::LOG_NORMAL)
        .value("UNIFORM",    LatencyDistType::UNIFORM)
        .export_values();

    py::class_<LatencyDistConfig>(m, "LatencyDistConfig")
        .def(py::init<>())
        .def_readwrite("type",  &LatencyDistConfig::type)
        .def_readwrite("mu",    &LatencyDistConfig::mu)
        .def_readwrite("sigma", &LatencyDistConfig::sigma)
        .def_readwrite("lo",    &LatencyDistConfig::lo)
        .def_readwrite("hi",    &LatencyDistConfig::hi)
        .def_static("fixed", [](double ns) {
            return LatencyDistConfig{LatencyDistType::FIXED, ns};
        }, py::arg("ns") = 500.0)
        .def_static("lognormal", [](double mu, double sigma) {
            return LatencyDistConfig{LatencyDistType::LOG_NORMAL, mu, sigma};
        }, py::arg("mu") = 5000.0, py::arg("sigma") = 1000.0);

    // ── World ─────────────────────────────────────────────────────────────────

    py::class_<World>(m, "World", "The simulation world.")
        .def(py::init([]() {
            return std::make_unique<World>(MatchingEngine{});
        }))
        .def("add_agent",
            [](World& w, IAgent* agent) {
                w.add_agent(std::unique_ptr<IAgent>(agent));
            },
            py::arg("agent"), py::keep_alive<1, 2>())
        .def("prefill_book",
            [](World& w, Price mid, int levels, Qty qty_per_level) {
                static uint64_t seed_id = 0xFFFF0000ULL; // avoids collision with agent-generated IDs
                for (int i = 1; i <= levels; ++i) {
                    Order bid{};
                    bid.id        = seed_id++;
                    bid.owner     = 0;
                    bid.side      = Side::Buy;
                    bid.type      = OrderType::Limit;
                    bid.price     = mid - static_cast<Price>(i);
                    bid.qty       = qty_per_level;
                    bid.ts        = 0;
                    bid.tif       = TimeInForce::GTC;
                    bid.mkt_style = MarketStyle::PureMarket;
                    w.engine_mut().process(bid);

                    Order ask{};
                    ask.id        = seed_id++;
                    ask.owner     = 0;
                    ask.side      = Side::Sell;
                    ask.type      = OrderType::Limit;
                    ask.price     = mid + static_cast<Price>(i);
                    ask.qty       = qty_per_level;
                    ask.ts        = 0;
                    ask.tif       = TimeInForce::GTC;
                    ask.mkt_style = MarketStyle::PureMarket;
                    w.engine_mut().process(ask);
                }
            },
            py::arg("mid"), py::arg("levels") = 20, py::arg("qty") = 10)
        .def("l2_snapshot",
            [](World& w, int levels) {
                auto bids = w.engine().book().depth(
                    Side::Buy,  static_cast<std::size_t>(levels));
                auto asks = w.engine().book().depth(
                    Side::Sell, static_cast<std::size_t>(levels));
                py::dict d;
                d["bids"] = bids;
                d["asks"] = asks;
                return d;
            }, py::arg("levels") = 10)
        .def("run",
            [](World& w, uint64_t seed, double horizon,
               WorldConfig cfg) -> WorldResult {
                py::gil_scoped_release release;
                return w.run(seed, horizon, cfg);
            },
            py::arg("seed"), py::arg("horizon"),
            py::arg("config") = WorldConfig{});

    // ── Built-in agent configs ────────────────────────────────────────────────

    py::class_<FundamentalValueConfig>(m, "FundamentalValueConfig")
        .def(py::init<>())
        .def_readwrite("kappa",     &FundamentalValueConfig::kappa)
        .def_readwrite("sigma_v",   &FundamentalValueConfig::sigma_v)
        .def_readwrite("threshold", &FundamentalValueConfig::threshold)
        .def_readwrite("lot_size",  &FundamentalValueConfig::lot_size);

    py::class_<MomentumConfig>(m, "MomentumConfig")
        .def(py::init<>())
        .def_readwrite("alpha_fast",   &MomentumConfig::alpha_fast)
        .def_readwrite("alpha_slow",   &MomentumConfig::alpha_slow)
        .def_readwrite("entry_band",   &MomentumConfig::entry_band)
        .def_readwrite("exit_band",    &MomentumConfig::exit_band)
        .def_readwrite("lot_size",     &MomentumConfig::lot_size)
        .def_readwrite("max_position", &MomentumConfig::max_position)
        .def_readwrite("warmup_steps", &MomentumConfig::warmup_steps);

    py::class_<HawkesNoiseConfig>(m, "HawkesNoiseConfig")
        .def(py::init<>())
        .def_readwrite("p_market",       &HawkesNoiseConfig::p_market)
        .def_readwrite("min_offset",     &HawkesNoiseConfig::min_offset)
        .def_readwrite("max_offset",     &HawkesNoiseConfig::max_offset)
        .def_readwrite("imbalance_bias", &HawkesNoiseConfig::imbalance_bias)
        .def_readwrite("lot_size",       &HawkesNoiseConfig::lot_size)
        .def_readwrite("dt_ns",          &HawkesNoiseConfig::dt_ns);

    py::class_<MarketMakerASConfig>(m, "MarketMakerASConfig")
        .def(py::init<>())
        .def_readwrite("gamma",                 &MarketMakerASConfig::gamma)
        .def_readwrite("kappa",                 &MarketMakerASConfig::kappa)
        .def_readwrite("T_steps",               &MarketMakerASConfig::T_steps)
        .def_readwrite("sigma_init",            &MarketMakerASConfig::sigma_init)
        .def_readwrite("sigma_ewma",            &MarketMakerASConfig::sigma_ewma)
        .def_readwrite("alpha_imb",             &MarketMakerASConfig::alpha_imb)
        .def_readwrite("lot_size",              &MarketMakerASConfig::lot_size)
        .def_readwrite("max_inv",               &MarketMakerASConfig::max_inv)
        .def_readwrite("min_half_spread_ticks", &MarketMakerASConfig::min_half_spread_ticks)
        .def_readwrite("warmup",                &MarketMakerASConfig::warmup);

    py::class_<VWAPConfig>(m, "VWAPConfig")
        .def(py::init<>())
        .def_readwrite("total_qty",            &VWAPConfig::total_qty)
        .def_readwrite("side",                 &VWAPConfig::side)
        .def_readwrite("n_buckets",            &VWAPConfig::n_buckets)
        .def_readwrite("schedule",             &VWAPConfig::schedule)
        .def_readwrite("custom_weights",       &VWAPConfig::custom_weights)
        .def_readwrite("use_limit",            &VWAPConfig::use_limit)
        .def_readwrite("limit_patience_steps", &VWAPConfig::limit_patience_steps)
        .def_readwrite("limit_offset_ticks",   &VWAPConfig::limit_offset_ticks)
        .def_readwrite("urgency_threshold",    &VWAPConfig::urgency_threshold);

    py::class_<TWAPConfig>(m, "TWAPConfig")
        .def(py::init<>())
        .def_readwrite("total_qty",            &TWAPConfig::total_qty)
        .def_readwrite("side",                 &TWAPConfig::side)
        .def_readwrite("use_limit",            &TWAPConfig::use_limit)
        .def_readwrite("limit_patience_steps", &TWAPConfig::limit_patience_steps)
        .def_readwrite("limit_offset_ticks",   &TWAPConfig::limit_offset_ticks)
        .def_readwrite("min_child_qty",        &TWAPConfig::min_child_qty);

    py::class_<ISConfig>(m, "ISConfig")
        .def(py::init<>())
        .def_readwrite("total_qty",      &ISConfig::total_qty)
        .def_readwrite("side",           &ISConfig::side)
        .def_readwrite("risk_aversion",  &ISConfig::risk_aversion)
        .def_readwrite("sigma",          &ISConfig::sigma)
        .def_readwrite("eta",            &ISConfig::eta)
        .def_readwrite("gamma",          &ISConfig::gamma)
        .def_readwrite("adapt_interval", &ISConfig::adapt_interval)
        .def_readwrite("sigma_ewma",     &ISConfig::sigma_ewma);

    py::class_<HawkesConfig>(m, "HawkesConfig")
        .def(py::init<>())
        .def_readwrite("mu",    &HawkesConfig::mu)
        .def_readwrite("alpha", &HawkesConfig::alpha)
        .def_readwrite("beta",  &HawkesConfig::beta);

    // ── Built-in agent classes ────────────────────────────────────────────────

    py::module_ agents = m.def_submodule("agents",
        "Built-in MSIM agent implementations.");

    py::class_<FundamentalValueAgent, IAgent,
    std::unique_ptr<FundamentalValueAgent, py::nodelete>>(agents,
        "FundamentalValueAgent")
        .def(py::init<OwnerId, FundamentalValueConfig>(),
             py::arg("owner_id"),
             py::arg("config") = FundamentalValueConfig{})
        .def("fundamental_value", &FundamentalValueAgent::fundamental_value);

    py::class_<MomentumAgent, IAgent,
    std::unique_ptr<MomentumAgent, py::nodelete>>(agents, "MomentumAgent")
        .def(py::init<OwnerId, MomentumConfig>(),
             py::arg("owner_id"),
             py::arg("config") = MomentumConfig{})
        .def("signal",   &MomentumAgent::signal)
        .def("position", &MomentumAgent::position);

    py::class_<HawkesNoiseTrader, IAgent,
    std::unique_ptr<HawkesNoiseTrader, py::nodelete>>(agents, "HawkesNoiseTrader")
        .def(py::init<OwnerId, HawkesNoiseConfig>(),
             py::arg("owner_id"),
             py::arg("config") = HawkesNoiseConfig{})
        .def("hawkes_intensity",      &HawkesNoiseTrader::hawkes_intensity)
        .def("hawkes_mean_intensity", &HawkesNoiseTrader::hawkes_mean_intensity);

    py::class_<MarketMakerAS, IAgent,
    std::unique_ptr<MarketMakerAS, py::nodelete>>(agents, "MarketMakerAS")
        .def(py::init<OwnerId, MarketMakerASConfig>(),
             py::arg("owner_id"),
             py::arg("config") = MarketMakerASConfig{})
        .def("sigma",       &MarketMakerAS::sigma)
        .def("has_resting", &MarketMakerAS::has_resting);

    py::class_<VWAPAgent, IAgent,
    std::unique_ptr<VWAPAgent, py::nodelete>>(agents, "VWAPAgent")
        .def(py::init<OwnerId, VWAPConfig>(),
             py::arg("owner_id"),
             py::arg("config") = VWAPConfig{})
        .def("set_total_steps", &VWAPAgent::set_total_steps, py::arg("n"))
        .def("is_done",         &VWAPAgent::is_done)
        .def("qty_executed",    &VWAPAgent::qty_executed)
        .def("qty_remaining",   &VWAPAgent::qty_remaining)
        .def("arrival_price",   &VWAPAgent::arrival_price)
        .def("pct_complete",    &VWAPAgent::pct_complete);

    py::class_<TWAPAgent, IAgent,
    std::unique_ptr<TWAPAgent, py::nodelete>>(agents, "TWAPAgent")
        .def(py::init<OwnerId, int, TWAPConfig>(),
             py::arg("owner_id"),
             py::arg("horizon_steps"),
             py::arg("config") = TWAPConfig{})
        .def("is_done",       &TWAPAgent::is_done)
        .def("qty_executed",  &TWAPAgent::qty_executed)
        .def("qty_remaining", &TWAPAgent::qty_remaining)
        .def("arrival_price", &TWAPAgent::arrival_price)
        .def("pct_complete",  &TWAPAgent::pct_complete);

    py::class_<ISAgent, IAgent,
    std::unique_ptr<ISAgent, py::nodelete>>(agents, "ISAgent")
        .def(py::init<OwnerId, int, ISConfig>(),
             py::arg("owner_id"),
             py::arg("horizon_steps"),
             py::arg("config") = ISConfig{})
        .def("is_done",       &ISAgent::is_done)
        .def("qty_executed",  &ISAgent::qty_executed)
        .def("qty_remaining", &ISAgent::qty_remaining)
        .def("arrival_price", &ISAgent::arrival_price)
        .def("urgency",       &ISAgent::urgency)
        .def("expected_is",   &ISAgent::expected_is)
        .def("pct_complete",  &ISAgent::pct_complete);

    m.attr("__version__") = "2.1.1";
    m.attr("__author__")  = "MSIM Contributors";
}
