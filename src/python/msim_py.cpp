// ============================================================
// src/python/msim_py.cpp
//
// pybind11 bindings for MSIM.
//
// Design principles:
//   1. Python agents subclass Agent and implement step() returning
//      a list of Action objects — more Pythonic than mutating an
//      output parameter.
//   2. All C++ optional<Price> fields surface as Python int | None.
//   3. WorldResult carries .trades_df() / .tops_df() helpers that
//      return numpy-backed data when numpy/pandas is available.
//   4. Built-in C++ agents (NoiseTrader, MarketMaker, FVAgent,
//      MomentumAgent, etc.) are directly exposed — no reimplementation.
//   5. Zero breaking changes to the existing C++ library.
//
// Build: enabled via CMake option MSIM_BUILD_PYTHON=ON
// ============================================================

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

namespace py = pybind11;
using namespace msim;
using namespace msim::agents;

// ─── Trampoline: lets Python subclass IAgent ─────────────────────────────────
//
// Python step() signature:
//   def step(self, ts: int, view: MarketView, state: AgentState) -> list[Action]
//
// The trampoline collects the returned list and appends to the C++ out-vector.
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

        // Accept list, tuple, or any iterable of Action
        for (auto item : ret) {
            out.push_back(item.cast<Action>());
        }
    }
};

// ─── Helper: optional<Price> → py::object (int or None) ──────────────────────
static py::object opt_to_py(const std::optional<Price>& opt) {
    if (opt) return py::int_(*opt);
    return py::none();
}

// ─── Module definition ────────────────────────────────────────────────────────
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
        >>> world.add_agent(msim.agents.NoiseTrader(owner_id=1))
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

    // ── Order ────────────────────────────────────────────────────────────────

    py::class_<Order>(m, "Order", R"pbdoc(
        A single order submitted to the matching engine.

        Attributes
        ----------
        id : int
            Unique order ID (must be globally unique per simulation run).
            Convention: ``(owner_id << 24) | counter``.
        owner : int
            Agent owner ID.
        side : Side
            Buy or Sell.
        type : OrderType
            Limit or Market.
        price : int
            Limit price in ticks. Ignored for Market orders.
        qty : int
            Order quantity in lots.
        ts : int
            Submission timestamp (nanoseconds). World overwrites this.
        tif : TimeInForce
            GTC, IOC, or FOK.
        mkt_style : MarketStyle
            PureMarket (cancel remainder) or MarketToLimit.
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

    py::class_<Trade>(m, "Trade", R"pbdoc(
        A completed trade (fill).

        Attributes
        ----------
        id              : int
        ts              : int  (nanoseconds)
        price           : int  (ticks)
        qty             : int  (lots)
        maker_order_id  : int
        taker_order_id  : int
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("id",             &Trade::id)
        .def_readwrite("ts",             &Trade::ts)
        .def_readwrite("price",          &Trade::price)
        .def_readwrite("qty",            &Trade::qty)
        .def_readwrite("maker_order_id", &Trade::maker_order_id)
        .def_readwrite("taker_order_id", &Trade::taker_order_id)
        .def("__repr__", [](const Trade& t) {
            return "<Trade ts=" + std::to_string(t.ts)
                 + " px=" + std::to_string(t.price)
                 + " qty=" + std::to_string(t.qty) + ">";
        });

    // ── BookTop ──────────────────────────────────────────────────────────────

    py::class_<BookTop>(m, "BookTop", "Top-of-book snapshot at one timestamp.")
        .def(py::init<>())
        .def_readwrite("ts", &BookTop::ts)
        .def_property_readonly("best_bid",
            [](const BookTop& b) { return opt_to_py(b.best_bid); })
        .def_property_readonly("best_ask",
            [](const BookTop& b) { return opt_to_py(b.best_ask); })
        .def_property_readonly("mid",
            [](const BookTop& b) { return opt_to_py(b.mid); })
        .def("__repr__", [](const BookTop& b) {
            std::string s = "<BookTop ts=" + std::to_string(b.ts);
            if (b.best_bid) s += " bid=" + std::to_string(*b.best_bid);
            if (b.best_ask) s += " ask=" + std::to_string(*b.best_ask);
            return s + ">";
        });

    // ── LevelSummary ─────────────────────────────────────────────────────────

    py::class_<LevelSummary>(m, "LevelSummary", "One price level in an L2 snapshot.")
        .def(py::init<>())
        .def_readwrite("price",       &LevelSummary::price)
        .def_readwrite("total_qty",   &LevelSummary::total_qty)
        .def_readwrite("order_count", &LevelSummary::order_count);

    // ── MarketView ───────────────────────────────────────────────────────────

    py::class_<MarketView>(m, "MarketView", R"pbdoc(
        Read-only snapshot of market state delivered to each agent every step.

        Attributes
        ----------
        ts          : int   — step timestamp (nanoseconds)
        best_bid    : int | None
        best_ask    : int | None
        mid         : int | None
        last_trade  : int | None
        bid_depth   : int   — quantity at best bid
        ask_depth   : int   — quantity at best ask
        imbalance   : float — (bid_qty - ask_qty) / (bid_qty + ask_qty), in [-1, 1]
    )pbdoc")
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
        }, "True when both best_bid and best_ask are present.")
        .def("spread", [](const MarketView& v) -> py::object {
            if (v.best_bid && v.best_ask)
                return py::int_(*v.best_ask - *v.best_bid);
            return py::none();
        }, "Bid-ask spread in ticks, or None if book is empty.");

    // ── AgentState ───────────────────────────────────────────────────────────

    py::class_<AgentState>(m, "AgentState", R"pbdoc(
        Per-agent account state delivered alongside MarketView each step.

        Attributes
        ----------
        owner       : int
        cash_ticks  : int   — cumulative cash P&L in ticks
        position    : int   — signed inventory (+ = long)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("owner",      &AgentState::owner)
        .def_readwrite("cash_ticks", &AgentState::cash_ticks)
        .def_readwrite("position",   &AgentState::position);

    // ── Action ───────────────────────────────────────────────────────────────

    py::class_<Action>(m, "Action", R"pbdoc(
        An instruction from an agent to the matching engine.

        Use the static factory methods:
            Action.submit(order)
            Action.cancel(order_id)
            Action.modify_qty(order_id, new_qty)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("type",    &Action::type)
        .def_readwrite("order",   &Action::order)
        .def_readwrite("id",      &Action::id)
        .def_readwrite("new_qty", &Action::new_qty)
        .def_static("submit",
            &Action::submit,
            py::arg("order"),
            "Submit a new order to the book.")
        .def_static("cancel",
            &Action::cancel,
            py::arg("order_id"),
            "Cancel a resting order by ID.")
        .def_static("modify_qty",
            &Action::modify_qty,
            py::arg("order_id"),
            py::arg("new_qty"),
            "Reduce quantity of a resting order (reduce-only).")
        .def("__repr__", [](const Action& a) {
            const char* t = a.type == ActionType::Submit    ? "Submit"
                          : a.type == ActionType::Cancel    ? "Cancel"
                                                            : "ModifyQty";
            return std::string("<Action ") + t + ">";
        });

    // ── IAgent (abstract base for Python strategies) ──────────────────────────

    py::class_<IAgent, PyAgent, std::unique_ptr<IAgent>>(m, "Agent", R"pbdoc(
        Abstract base class for all MSIM agents.

        Subclass this to implement a custom strategy::

            class MyStrategy(msim.Agent):
                def __init__(self, owner_id: int):
                    super().__init__()
                    self._owner = owner_id
                    self._rng   = None

                def owner(self) -> int:
                    return self._owner

                def seed(self, s: int) -> None:
                    import random
                    self._rng = random.Random(s)

                def step(self, ts: int, view: msim.MarketView,
                         state: msim.AgentState) -> list:
                    if not view.has_quote():
                        return []
                    o = msim.Order()
                    o.id        = self._owner * 10000 + ts % 10000
                    o.owner     = self._owner
                    o.side      = msim.Side.Buy
                    o.type      = msim.OrderType.Market
                    o.qty       = 1
                    o.tif       = msim.TimeInForce.IOC
                    o.mkt_style = msim.MarketStyle.PureMarket
                    return [msim.Action.submit(o)]

        Notes
        -----
        - ``step()`` must return a list (possibly empty) of ``Action`` objects.
        - ``seed()`` is called once before the simulation begins.
        - The World calls ``step()`` once per timestep per agent.
    )pbdoc")
        .def(py::init<>())
        .def("owner", &IAgent::owner,
             "Return the unique OwnerId for this agent.")
        .def("seed", &IAgent::seed,
             py::arg("s"),
             "Seed the agent's RNG. Called once before run() begins.")
        .def("step", [](IAgent& /*a*/, Ts, const MarketView&,
                        const AgentState&) -> py::list {
                return py::list();
             },
             py::arg("ts"), py::arg("view"), py::arg("state"),
             "Called every timestep. Return a list of Action objects.");

    // ── FVLogEntry ───────────────────────────────────────────────────────────

    py::class_<FVLogEntry>(m, "FVLogEntry",
        "Private fundamental value log entry (one per FVAgent per step).")
        .def_readwrite("ts",    &FVLogEntry::ts)
        .def_readwrite("owner", &FVLogEntry::owner)
        .def_readwrite("V",     &FVLogEntry::V);

    // ── StyleFacts ───────────────────────────────────────────────────────────

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

    py::class_<StyleFacts>(m, "StyleFacts", R"pbdoc(
        Computed stylized facts for a completed simulation run.

        Validation flags (True = consistent with real market behavior):
            fat_tails_ok       — excess kurtosis > 1.0
            vol_clustering_ok  — |return| AC lag-1 > 0.05
            flow_autocorr_ok   — trade-sign AC lag-1 > 0.10
            positive_spread_ok — time-weighted spread > 0
            positive_impact_ok — Kyle's lambda != 0
    )pbdoc")
        .def_readwrite("returns",          &StyleFacts::returns)
        .def_readwrite("autocorr",         &StyleFacts::autocorr)
        .def_readwrite("impact",           &StyleFacts::impact)
        .def_readwrite("spreads",          &StyleFacts::spreads)
        .def_readwrite("amihud",           &StyleFacts::amihud)
        .def_readwrite("fat_tails_ok",     &StyleFacts::fat_tails_ok)
        .def_readwrite("vol_clustering_ok",&StyleFacts::vol_clustering_ok)
        .def_readwrite("flow_autocorr_ok", &StyleFacts::flow_autocorr_ok)
        .def_readwrite("positive_spread_ok",&StyleFacts::positive_spread_ok)
        .def_readwrite("positive_impact_ok",&StyleFacts::positive_impact_ok)
        .def("summary", &StylizedFactsMeasurer::summary,
             "Return a human-readable report string.")
        .def("passes", [](const StyleFacts& sf) {
            return sf.fat_tails_ok && sf.vol_clustering_ok
                && sf.flow_autocorr_ok && sf.positive_spread_ok
                && sf.positive_impact_ok;
        }, "True when all five validation checks pass.");

    // ── AccountSnapshot ───────────────────────────────────────────────────────

    py::class_<AccountSnapshot>(m, "AccountSnapshot",
        "Final mark-to-market account state for one agent.")
        .def_readwrite("owner",      &AccountSnapshot::owner)
        .def_readwrite("cash_ticks", &AccountSnapshot::cash_ticks)
        .def_readwrite("position",   &AccountSnapshot::position);

    // ── WorldResult ──────────────────────────────────────────────────────────

    py::class_<WorldResult>(m, "WorldResult", R"pbdoc(
        Output from a completed World.run() call.

        Attributes
        ----------
        trades          : list[Trade]
        tops            : list[BookTop]
        accounts        : list[AccountSnapshot]
        cancel_failures : int
        modify_failures : int
        sf              : StyleFacts | None
        fv_log          : list[FVLogEntry]

        Methods
        -------
        trades_df()   — pandas DataFrame of all trades
        tops_df()     — pandas DataFrame of top-of-book snapshots
        accounts_df() — pandas DataFrame of final account states
        fv_df()       — pandas DataFrame of FV signal log
        summary()     — print stylized facts report
    )pbdoc")
        .def_readwrite("trades",          &WorldResult::trades)
        .def_readwrite("tops",            &WorldResult::tops)
        .def_readwrite("accounts",        &WorldResult::accounts)
        .def_readwrite("cancel_failures", &WorldResult::cancel_failures)
        .def_readwrite("modify_failures", &WorldResult::modify_failures)
        .def_readwrite("fv_log",          &WorldResult::fv_log)
        .def_property_readonly("sf", [](const WorldResult& r) -> py::object {
            if (r.sf) return py::cast(*r.sf);
            return py::none();
        }, "StyleFacts if compute_stylized_facts=True, else None.")

        // DataFrame helpers — only call pandas if it is actually installed
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
                rows.append(row);
            }
            return pd.attr("DataFrame")(rows);
        }, R"pbdoc(
            Return all trades as a pandas DataFrame.

            Columns: id, ts, price, qty, maker_order_id, taker_order_id

            Raises ImportError if pandas is not installed.
        )pbdoc")

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
        }, "Return top-of-book snapshots as a pandas DataFrame.")

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
        }, "Return final account states as a pandas DataFrame.")

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
        }, "Return FundamentalValueAgent signal log as a pandas DataFrame.")

        .def("summary", [](const WorldResult& r) {
            if (!r.sf) {
                py::print("No stylized facts computed (need compute_stylized_facts=True).");
                return;
            }
            py::print(StylizedFactsMeasurer::summary(*r.sf));
        }, "Print the stylized facts validation report.");

    // ── WorldConfig ───────────────────────────────────────────────────────────

    py::class_<WorldConfig>(m, "WorldConfig", R"pbdoc(
        Configuration for a World.run() call.

        Attributes
        ----------
        dt_ns                   : int   — step width in nanoseconds (default 1ms)
        latency_enabled         : bool  — enable per-agent latency model
        latency_configs         : list  — one LatencyDistConfig per agent
        compute_stylized_facts  : bool  — compute SF at end (default True)
        record_fv_signals       : bool  — log FV agent signals (default False)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("dt_ns",                  &WorldConfig::dt_ns)
        .def_readwrite("latency_enabled",         &WorldConfig::latency_enabled)
        .def_readwrite("latency_configs",         &WorldConfig::latency_configs)
        .def_readwrite("compute_stylized_facts",  &WorldConfig::compute_stylized_facts)
        .def_readwrite("record_fv_signals",       &WorldConfig::record_fv_signals);

    // ── LatencyDistConfig ─────────────────────────────────────────────────────

    py::enum_<LatencyDistType>(m, "LatencyDistType")
        .value("FIXED",      LatencyDistType::FIXED)
        .value("GAUSSIAN",   LatencyDistType::GAUSSIAN)
        .value("LOG_NORMAL", LatencyDistType::LOG_NORMAL)
        .value("UNIFORM",    LatencyDistType::UNIFORM)
        .export_values();

    py::class_<LatencyDistConfig>(m, "LatencyDistConfig",
        "Per-agent network latency distribution configuration.")
        .def(py::init<>())
        .def_readwrite("type",  &LatencyDistConfig::type)
        .def_readwrite("mu",    &LatencyDistConfig::mu)
        .def_readwrite("sigma", &LatencyDistConfig::sigma)
        .def_readwrite("lo",    &LatencyDistConfig::lo)
        .def_readwrite("hi",    &LatencyDistConfig::hi)
        .def_static("fixed",      [](double ns) {
            return LatencyDistConfig{LatencyDistType::FIXED, ns};
        }, py::arg("ns") = 500.0, "Constant delay of `ns` nanoseconds.")
        .def_static("lognormal",  [](double mu, double sigma) {
            return LatencyDistConfig{LatencyDistType::LOG_NORMAL, mu, sigma};
        }, py::arg("mu") = 5000.0, py::arg("sigma") = 1000.0,
        "Log-normal delay: mean=mu ns, shape=sigma ns.");

    // ── World ─────────────────────────────────────────────────────────────────

    py::class_<World>(m, "World", R"pbdoc(
        The simulation world: wraps the matching engine and dispatches agents.

        Example
        -------
        >>> world = msim.World()
        >>> world.prefill_book(mid=10000, levels=20, qty=10)
        >>> world.add_agent(MyStrategy(owner_id=1))
        >>> result = world.run(seed=42, horizon=2.0)
    )pbdoc")
        // Default constructor: creates a World with a default MatchingEngine
        .def(py::init([]() {
            return std::make_unique<World>(MatchingEngine{});
        }), "Create a World with a default (empty) matching engine.")

        .def("add_agent",
            [](World& w, std::unique_ptr<IAgent> agent) {
                w.add_agent(std::move(agent));
            },
            py::arg("agent"),
            py::keep_alive<1, 2>(),
            "Register an agent. Agents are called in registration order.")

        .def("prefill_book",
            [](World& w, Price mid, int levels, Qty qty_per_level) {
                // Pre-fill symmetric resting limit orders around mid.
                // Uses the same pattern as run_theory.cpp.
                static uint64_t seed_id = 9000;
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
            py::arg("mid"),
            py::arg("levels") = 20,
            py::arg("qty")    = 10,
            R"pbdoc(
                Pre-fill the order book with symmetric resting limit orders.

                Places `levels` bid and ask orders around `mid`, each with
                quantity `qty`. Prices are mid-i (bid) and mid+i (ask) for
                i in 1..levels. Call this before run() to start with a
                liquid book.
            )pbdoc")

        .def("l2_snapshot",
            [](World& w, int levels) {
                // Returns {"bids": [LevelSummary, ...], "asks": [...]}
                auto bids = w.engine().book().depth(Side::Buy,
                                static_cast<std::size_t>(levels));
                auto asks = w.engine().book().depth(Side::Sell,
                                static_cast<std::size_t>(levels));
                py::dict d;
                d["bids"] = bids;
                d["asks"] = asks;
                return d;
            },
            py::arg("levels") = 10,
            "Return a dict with 'bids' and 'asks' lists of LevelSummary.")

        .def("run",
            [](World& w, uint64_t seed, double horizon,
               WorldConfig cfg) -> WorldResult {
                py::gil_scoped_release release;
                return w.run(seed, horizon, cfg);
            },
            py::arg("seed"),
            py::arg("horizon"),
            py::arg("config") = WorldConfig{},
            R"pbdoc(
                Run the simulation and return a WorldResult.

                Parameters
                ----------
                seed    : int   — reproducibility seed
                horizon : float — simulation duration in seconds
                config  : WorldConfig — optional configuration

                Returns
                -------
                WorldResult
                    Contains all trades, top-of-book snapshots, final
                    account states, and optional stylized facts.

                Notes
                -----
                The GIL is released during the C++ simulation loop, so
                other Python threads can run concurrently. Python agents
                re-acquire the GIL in their step() override.
            )pbdoc");

    // ── Built-in agent configs ────────────────────────────────────────────────

    py::class_<FundamentalValueConfig>(m, "FundamentalValueConfig", R"pbdoc(
        Configuration for the Glosten-Milgrom informed trader.

        Attributes
        ----------
        kappa     : float — OU mean-reversion speed (default 0.005)
        sigma_v   : float — signal volatility in ticks/step (default 1.5)
        threshold : float — min mispricing in ticks to trade (default 1.0)
        lot_size  : int   — order size in lots (default 5)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("kappa",     &FundamentalValueConfig::kappa)
        .def_readwrite("sigma_v",   &FundamentalValueConfig::sigma_v)
        .def_readwrite("threshold", &FundamentalValueConfig::threshold)
        .def_readwrite("lot_size",  &FundamentalValueConfig::lot_size);

    py::class_<MomentumConfig>(m, "MomentumConfig", R"pbdoc(
        Configuration for the MACD momentum agent.

        Attributes
        ----------
        alpha_fast   : float — fast EMA coefficient (default 2/6)
        alpha_slow   : float — slow EMA coefficient (default 2/21)
        entry_band   : float — signal threshold to open position (default 0.30)
        exit_band    : float — signal threshold to flatten (default 0.05)
        lot_size     : int   — lots per signal (default 3)
        max_position : int   — inventory cap (default 15)
        warmup_steps : int   — steps before trading begins (default 20)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("alpha_fast",   &MomentumConfig::alpha_fast)
        .def_readwrite("alpha_slow",   &MomentumConfig::alpha_slow)
        .def_readwrite("entry_band",   &MomentumConfig::entry_band)
        .def_readwrite("exit_band",    &MomentumConfig::exit_band)
        .def_readwrite("lot_size",     &MomentumConfig::lot_size)
        .def_readwrite("max_position", &MomentumConfig::max_position)
        .def_readwrite("warmup_steps", &MomentumConfig::warmup_steps);

    py::class_<HawkesNoiseConfig>(m, "HawkesNoiseConfig", R"pbdoc(
        Configuration for the self-exciting noise trader.

        Attributes
        ----------
        hawkes         : HawkesConfig — arrival process parameters
        p_market       : float        — probability of market vs limit order
        min_offset     : int          — min ticks from mid for limit orders
        max_offset     : int          — max ticks from mid for limit orders
        imbalance_bias : float        — sensitivity to LOB imbalance [0, 1]
        lot_size       : int          — lots per order
        dt_ns          : int          — step width in ns (match WorldConfig)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("p_market",       &HawkesNoiseConfig::p_market)
        .def_readwrite("min_offset",     &HawkesNoiseConfig::min_offset)
        .def_readwrite("max_offset",     &HawkesNoiseConfig::max_offset)
        .def_readwrite("imbalance_bias", &HawkesNoiseConfig::imbalance_bias)
        .def_readwrite("lot_size",       &HawkesNoiseConfig::lot_size)
        .def_readwrite("dt_ns",          &HawkesNoiseConfig::dt_ns);

    py::class_<MarketMakerASConfig>(m, "MarketMakerASConfig", R"pbdoc(
        Configuration for the Avellaneda-Stoikov market maker.

        Attributes
        ----------
        gamma        : float — absolute risk aversion (default 0.01)
        kappa        : float — order arrival intensity (default 1.5)
        T_steps      : int   — rolling horizon in steps (default 500)
        sigma_init   : float — initial vol estimate in ticks/step (default 2.0)
        sigma_ewma   : float — EWMA decay for vol estimation (default 0.02)
        alpha_imb    : float — imbalance sensitivity [0,1] (default 0.5)
        lot_size     : int   — quote size (default 1)
        max_inv      : int   — inventory cap (default 20)
        warmup       : int   — steps before quoting (default 10)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("gamma",                    &MarketMakerASConfig::gamma)
        .def_readwrite("kappa",                    &MarketMakerASConfig::kappa)
        .def_readwrite("T_steps",                  &MarketMakerASConfig::T_steps)
        .def_readwrite("sigma_init",               &MarketMakerASConfig::sigma_init)
        .def_readwrite("sigma_ewma",               &MarketMakerASConfig::sigma_ewma)
        .def_readwrite("alpha_imb",                &MarketMakerASConfig::alpha_imb)
        .def_readwrite("lot_size",                 &MarketMakerASConfig::lot_size)
        .def_readwrite("max_inv",                  &MarketMakerASConfig::max_inv)
        .def_readwrite("min_half_spread_ticks",    &MarketMakerASConfig::min_half_spread_ticks)
        .def_readwrite("warmup",                   &MarketMakerASConfig::warmup);

    // ── Built-in agent classes ────────────────────────────────────────────────

    py::module_ agents = m.def_submodule("agents", "Built-in MSIM agent implementations.");

    py::class_<FundamentalValueAgent, IAgent,
               std::unique_ptr<FundamentalValueAgent>>(agents, "FundamentalValueAgent", R"pbdoc(
        Glosten-Milgrom informed trader with Ornstein-Uhlenbeck private signal.

        Buys when V_t - ask > threshold, sells when bid - V_t > threshold.
    )pbdoc")
        .def(py::init<OwnerId, FundamentalValueConfig>(),
             py::arg("owner_id"),
             py::arg("config") = FundamentalValueConfig{})
        .def("fundamental_value", &FundamentalValueAgent::fundamental_value,
             "Current private signal value V_t.");

    py::class_<MomentumAgent, IAgent,
               std::unique_ptr<MomentumAgent>>(agents, "MomentumAgent", R"pbdoc(
        MACD trend-following agent.

        Enters long when EMA_fast - EMA_slow > entry_band.
        Enters short when EMA_fast - EMA_slow < -entry_band.
        Flattens when |signal| < exit_band.
    )pbdoc")
        .def(py::init<OwnerId, MomentumConfig>(),
             py::arg("owner_id"),
             py::arg("config") = MomentumConfig{})
        .def("signal",   &MomentumAgent::signal,   "Current MACD signal value.")
        .def("position", &MomentumAgent::position, "Current signed inventory.");

    py::class_<HawkesNoiseTrader, IAgent,
               std::unique_ptr<HawkesNoiseTrader>>(agents, "HawkesNoiseTrader", R"pbdoc(
        Noise trader with Hawkes (self-exciting) order arrival process.

        Order flow clusters in time, reproducing empirical intraday patterns.
    )pbdoc")
        .def(py::init<OwnerId, HawkesNoiseConfig>(),
             py::arg("owner_id"),
             py::arg("config") = HawkesNoiseConfig{})
        .def("hawkes_intensity",      &HawkesNoiseTrader::hawkes_intensity,
             "Current Hawkes process intensity (orders/second).")
        .def("hawkes_mean_intensity", &HawkesNoiseTrader::hawkes_mean_intensity,
             "Stationary mean intensity.");

    py::class_<MarketMakerAS, IAgent,
               std::unique_ptr<MarketMakerAS>>(agents, "MarketMakerAS", R"pbdoc(
        Avellaneda-Stoikov optimal market maker with LOB imbalance skew.

        Quotes at r ± δ*/2 where r is the inventory-adjusted reservation price
        and δ* is the optimal spread derived from the A-S (2008) model.
    )pbdoc")
        .def(py::init<OwnerId, MarketMakerASConfig>(),
             py::arg("owner_id"),
             py::arg("config") = MarketMakerASConfig{})
        .def("sigma",       &MarketMakerAS::sigma,      "Current vol estimate (ticks/step).")
        .def("has_resting", &MarketMakerAS::has_resting,"True when the MM has active quotes.");

    // ── HawkesConfig ─────────────────────────────────────────────────────────

    py::class_<HawkesConfig>(m, "HawkesConfig",
        "Hawkes process parameters: mu (baseline), alpha (excitation), beta (decay).")
        .def(py::init<>())
        .def_readwrite("mu",    &HawkesConfig::mu)
        .def_readwrite("alpha", &HawkesConfig::alpha)
        .def_readwrite("beta",  &HawkesConfig::beta);

    // ── Version ───────────────────────────────────────────────────────────────

    m.attr("__version__") = "0.1.0";
    m.attr("__author__")  = "MSIM Contributors";
}
