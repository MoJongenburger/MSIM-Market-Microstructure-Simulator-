// ============================================================
// PATCH for src/python/msim_py.cpp
//
// Add these includes at the top of msim_py.cpp (with the others):
//   #include "msim/agents/vwap_agent.hpp"
//   #include "msim/agents/twap_agent.hpp"
//   #include "msim/agents/is_agent.hpp"
//
// Then append these blocks inside PYBIND11_MODULE, after the
// existing agents submodule definitions and before the final }.
// ============================================================

    // ── Execution agent configs ───────────────────────────────────────────────

    py::enum_<VWAPSchedule>(m, "VWAPSchedule",
        "VWAP execution schedule type.")
        .value("FLAT",    VWAPSchedule::FLAT,
               "Uniform weight — equivalent to TWAP in execution.")
        .value("U_SHAPE", VWAPSchedule::U_SHAPE,
               "Higher weight at open/close. Matches typical equity intraday pattern.")
        .value("CUSTOM",  VWAPSchedule::CUSTOM,
               "User-supplied weight vector (set in VWAPConfig.custom_weights).")
        .export_values();

    py::class_<VWAPConfig>(m, "VWAPConfig", R"pbdoc(
        Configuration for the VWAP execution agent.

        Attributes
        ----------
        total_qty             : int          — total lots to execute
        side                  : Side         — Buy or Sell
        n_buckets             : int          — number of time buckets (default 20)
        schedule              : VWAPSchedule — FLAT, U_SHAPE, or CUSTOM
        custom_weights        : list[float]  — weights when schedule=CUSTOM
        use_limit             : bool         — try limit orders first (default False)
        limit_patience_steps  : int          — steps before cancelling limit (default 3)
        limit_offset_ticks    : int          — ticks inside spread for limit (default 1)
        urgency_threshold     : float        — fraction behind schedule before forced market
    )pbdoc")
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

    py::class_<TWAPConfig>(m, "TWAPConfig", R"pbdoc(
        Configuration for the TWAP execution agent.

        Attributes
        ----------
        total_qty             : int   — total lots to execute
        side                  : Side  — Buy or Sell
        use_limit             : bool  — try limit orders first (default False)
        limit_patience_steps  : int   — steps before cancelling limit (default 5)
        limit_offset_ticks    : int   — ticks inside spread for limit (default 0)
        min_child_qty         : int   — minimum child order size (default 1)
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("total_qty",            &TWAPConfig::total_qty)
        .def_readwrite("side",                 &TWAPConfig::side)
        .def_readwrite("use_limit",            &TWAPConfig::use_limit)
        .def_readwrite("limit_patience_steps", &TWAPConfig::limit_patience_steps)
        .def_readwrite("limit_offset_ticks",   &TWAPConfig::limit_offset_ticks)
        .def_readwrite("min_child_qty",        &TWAPConfig::min_child_qty);

    py::class_<ISConfig>(m, "ISConfig", R"pbdoc(
        Configuration for the Almgren-Chriss Implementation Shortfall agent.

        Attributes
        ----------
        total_qty       : int   — total lots to execute
        side            : Side  — Buy or Sell
        risk_aversion   : float — λ: 0 = TWAP, 0.01 = moderate, 0.1 = aggressive
        sigma           : float — price volatility in ticks/step (default 2.0)
        eta             : float — temporary impact coefficient ticks/lot (default 0.5)
        gamma           : float — permanent impact coefficient ticks/lot (default 0.25)
        adapt_interval  : int   — steps between trajectory recomputation (0 = fixed)
        sigma_ewma      : float — EWMA alpha for vol estimation (0 = use cfg.sigma)

        Notes
        -----
        The Almgren-Chriss optimal trajectory is pre-computed in seed().
        With risk_aversion=0, the trajectory degenerates to TWAP.
        With high risk_aversion, execution front-loads aggressively.
    )pbdoc")
        .def(py::init<>())
        .def_readwrite("total_qty",      &ISConfig::total_qty)
        .def_readwrite("side",           &ISConfig::side)
        .def_readwrite("risk_aversion",  &ISConfig::risk_aversion)
        .def_readwrite("sigma",          &ISConfig::sigma)
        .def_readwrite("eta",            &ISConfig::eta)
        .def_readwrite("gamma",          &ISConfig::gamma)
        .def_readwrite("adapt_interval", &ISConfig::adapt_interval)
        .def_readwrite("sigma_ewma",     &ISConfig::sigma_ewma);

    // ── Execution agent classes ───────────────────────────────────────────────
    // Added to the existing agents submodule.

    py::class_<VWAPAgent, IAgent,
               std::unique_ptr<VWAPAgent>>(agents, "VWAPAgent", R"pbdoc(
        VWAP execution agent.

        Slices a parent order into child orders proportional to a volume
        schedule. Three schedule types: FLAT (=TWAP), U_SHAPE (empirical
        equity intraday pattern), or CUSTOM (user-supplied weights).

        Parameters
        ----------
        owner_id : int
        config   : VWAPConfig

        Notes
        -----
        Call set_total_steps(n) after construction to match the simulation
        horizon. If not called, defaults to 2000 steps.

        Example
        -------
        >>> cfg = msim.VWAPConfig()
        >>> cfg.total_qty = 500
        >>> cfg.side      = msim.Side.Buy
        >>> cfg.schedule  = msim.VWAPSchedule.U_SHAPE
        >>> agent = msim.agents.VWAPAgent(owner_id=50, config=cfg)
        >>> agent.set_total_steps(2000)   # match horizon_steps
    )pbdoc")
        .def(py::init<OwnerId, VWAPConfig>(),
             py::arg("owner_id"),
             py::arg("config") = VWAPConfig{})
        .def("set_total_steps", &VWAPAgent::set_total_steps,
             py::arg("n"),
             "Set horizon in steps (must match World simulation horizon).")
        .def("is_done",        &VWAPAgent::is_done,
             "True when the full parent order has been executed.")
        .def("qty_executed",   &VWAPAgent::qty_executed,
             "Lots executed so far.")
        .def("qty_remaining",  &VWAPAgent::qty_remaining,
             "Lots still to execute.")
        .def("arrival_price",  &VWAPAgent::arrival_price,
             "Mid-price at the start of execution.")
        .def("pct_complete",   &VWAPAgent::pct_complete,
             "Percentage of parent order executed (0-100).");

    py::class_<TWAPAgent, IAgent,
               std::unique_ptr<TWAPAgent>>(agents, "TWAPAgent", R"pbdoc(
        TWAP execution agent.

        Uniform time slicing — the simplest execution baseline.
        Child order size = ceil(remaining / remaining_steps).

        Parameters
        ----------
        owner_id       : int
        horizon_steps  : int        — total steps over which to slice
        config         : TWAPConfig

        Example
        -------
        >>> cfg = msim.TWAPConfig()
        >>> cfg.total_qty = 500
        >>> cfg.side      = msim.Side.Buy
        >>> agent = msim.agents.TWAPAgent(owner_id=51, horizon_steps=2000, config=cfg)
    )pbdoc")
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
               std::unique_ptr<ISAgent>>(agents, "ISAgent", R"pbdoc(
        Almgren-Chriss Implementation Shortfall agent.

        Executes the optimal trajectory that minimises:
            U = E[IS] + risk_aversion * Var[IS]

        The trajectory is pre-computed via x_k = X * sinh(κ(T-k)) / sinh(κT).

        With risk_aversion=0 it degenerates to TWAP.
        With high risk_aversion it front-loads (acts immediately).

        Parameters
        ----------
        owner_id       : int
        horizon_steps  : int     — total simulation steps
        config         : ISConfig

        Example
        -------
        >>> cfg = msim.ISConfig()
        >>> cfg.total_qty     = 500
        >>> cfg.side          = msim.Side.Buy
        >>> cfg.risk_aversion = 0.01    # moderate urgency
        >>> cfg.sigma         = 2.0     # 2 ticks/step volatility
        >>> cfg.eta           = 0.5     # 0.5 ticks/lot temporary impact
        >>> agent = msim.agents.ISAgent(owner_id=52, horizon_steps=2000, config=cfg)
    )pbdoc")
        .def(py::init<OwnerId, int, ISConfig>(),
             py::arg("owner_id"),
             py::arg("horizon_steps"),
             py::arg("config") = ISConfig{})
        .def("is_done",       &ISAgent::is_done)
        .def("qty_executed",  &ISAgent::qty_executed)
        .def("qty_remaining", &ISAgent::qty_remaining)
        .def("arrival_price", &ISAgent::arrival_price)
        .def("urgency",       &ISAgent::urgency,
             "κ parameter: higher = more front-loaded execution.")
        .def("expected_is",   &ISAgent::expected_is,
             "Theoretical expected IS in ticks under current model params.")
        .def("pct_complete",  &ISAgent::pct_complete);
