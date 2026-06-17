\documentclass[12pt, a4paper]{article}

% ── Packages ─────────────────────────────────────────────────────────────────
\usepackage[T1]{fontenc}
\usepackage[utf8]{inputenc}
\usepackage{lmodern}
\usepackage[a4paper, top=2.5cm, bottom=2.5cm, left=2.8cm, right=2.8cm]{geometry}
\usepackage{amsmath, amssymb, amsthm}
\usepackage{mathtools}
\usepackage{booktabs}
\usepackage{multirow}
\usepackage{array}
\usepackage{tabularx}
\usepackage{longtable}
\usepackage{graphicx}
\usepackage{xcolor}
\usepackage{hyperref}
\usepackage{cleveref}
\usepackage{algorithm}
\usepackage{algpseudocode}
\usepackage{listings}
\usepackage{microtype}
\usepackage{setspace}
\usepackage{parskip}
\usepackage{caption}
\usepackage{subcaption}
\usepackage{natbib}
\usepackage{titlesec}
\usepackage{abstract}
\usepackage{fancyhdr}
\setlength{\headheight}{14.5pt}
\usepackage{enumitem}
\usepackage{siunitx}
\usepackage{bm}
\usepackage{tikz}
\usepackage{pgf}
\usetikzlibrary{shapes.geometric,shapes.misc,arrows.meta,positioning,
                fit,backgrounds,calc,decorations.pathreplacing}

% ── Hyperref setup ────────────────────────────────────────────────────────────
\hypersetup{
    colorlinks = true,
    linkcolor  = blue!70!black,
    citecolor  = green!50!black,
    urlcolor   = blue!70!black,
    pdftitle   = {MSIM: A Sub-50 Nanosecond Deterministic Market Microstructure Simulator},
    pdfauthor  = {Mo Jongenburger},
}

% ── Listings (code) ───────────────────────────────────────────────────────────
\lstdefinestyle{cppstyle}{
    language        = C++,
    basicstyle      = \ttfamily\small,
    keywordstyle    = \color{blue!80!black}\bfseries,
    commentstyle    = \color{green!50!black}\itshape,
    stringstyle     = \color{red!70!black},
    breaklines      = true,
    frame           = single,
    backgroundcolor = \color{gray!5},
    numbers         = left,
    numberstyle     = \tiny\color{gray},
    xleftmargin     = 1.5em,
}
\lstdefinestyle{pystyle}{
    language        = Python,
    basicstyle      = \ttfamily\small,
    keywordstyle    = \color{blue!80!black}\bfseries,
    commentstyle    = \color{green!50!black}\itshape,
    stringstyle     = \color{red!70!black},
    breaklines      = true,
    frame           = single,
    backgroundcolor = \color{gray!5},
    numbers         = left,
    numberstyle     = \tiny\color{gray},
    xleftmargin     = 1.5em,
}

% ── Theorem environments ──────────────────────────────────────────────────────
\newtheorem{proposition}{Proposition}
\newtheorem{claim}{Claim}
\newtheorem{definition}{Definition}
\newtheorem{remark}{Remark}

% ── Headers ───────────────────────────────────────────────────────────────────
\pagestyle{fancy}
\fancyhf{}
\fancyhead[L]{\small MSIM: Market Microstructure Simulator}
\fancyhead[R]{\small Jongenburger (2026)}
\fancyfoot[C]{\thepage}
\renewcommand{\headrulewidth}{0.4pt}

% ── Section formatting ────────────────────────────────────────────────────────
\titleformat{\section}{\large\bfseries}{\thesection}{1em}{}
\titleformat{\subsection}{\normalsize\bfseries}{\thesubsection}{1em}{}
\titleformat{\subsubsection}{\normalsize\itshape}{\thesubsubsection}{1em}{}

% ── Spacing ───────────────────────────────────────────────────────────────────
\onehalfspacing
\setlength{\parindent}{0pt}
\setlength{\parskip}{6pt}

% ── Column types ─────────────────────────────────────────────────────────────
\newcolumntype{R}[1]{>{\raggedleft\arraybackslash}p{#1}}
\newcolumntype{L}[1]{>{\raggedright\arraybackslash}p{#1}}
\newcolumntype{C}[1]{>{\centering\arraybackslash}p{#1}}

% ─────────────────────────────────────────────────────────────────────────────
\begin{document}

% ── Title page ────────────────────────────────────────────────────────────────
\begin{titlepage}
\centering
\vspace*{2cm}

{\LARGE\bfseries MSIM: A Sub-50 Nanosecond Deterministic\\[0.4em]
Market Microstructure Simulator in C++20\par}

\vspace{1.5cm}

{\large Mo Jongenburger\par}

\vspace{0.5cm}
{\normalsize\itshape
Independent Research\\
\href{https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-}%
     {github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-}\\[0.3em]
April 2026\par}

\vspace{2cm}

\begin{abstract}
\noindent
We present MSIM, an open-source, deterministic limit order book and matching engine simulator
written in C++20, designed as a high-performance research platform for market microstructure
analysis. The engine processes market orders in \SI{46.6}{\nano\second} (p50) on an Intel
Core i7-13700F, sustaining 21.9 million order operations per second, single-threaded. Systematic optimisation passes transformed a textbook implementation into a
cache-resident hot path; the four passes with direct benchmark impact improved mutation latency by 4$\times$--7$\times$ and depth queries
by 25.1$\times$. The primary contribution is the integrated platform: this performance enables
large-scale, multi-seed parameter studies and reinforcement learning settings where the
matching kernel---not agent logic---is the throughput bottleneck.

The simulator ships with a Python interface via pybind11, a nine-agent library spanning noise traders, market makers, and execution algorithms, and an
automatic stylised facts measurement layer. A 300-second simulation with nine heterogeneous
agents illustrates qualitative emergence of five canonical microstructure regularities,
including fat-tailed returns (excess kurtosis 4.04) and a stable positive bid-ask spread.
A 50-seed robustness study confirms four of the five regularities emerge at 96--100\,\%
pass rates among eligible seeds (92--100\,\% across all 50 seeds); one degenerate and one catastrophic-spread seed are documented as limitations. Parameters were selected for dynamic stability rather than calibrated to
empirical data; the results constitute a demonstration of platform expressiveness, not a
replication of a specific real market. MSIM is released under the MIT licence.
\end{abstract}

\vspace{1cm}
{\small\textbf{Keywords:} market microstructure, limit order book, matching engine, agent-based simulation, high-frequency finance, stylised facts, C++20, cache optimisation}

\end{titlepage}

\tableofcontents
\newpage

% ─────────────────────────────────────────────────────────────────────────────
\section{Introduction}
\label{sec:intro}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Motivation}

Market microstructure theory, originating with \citet{demsetz1968} and formalised by \citet{glosten1985} and \citet{kyle1985}, studies the relationship between the rules governing order submission and matching on one side, and the emergent properties of price formation, bid-ask spreads, and market quality on the other. A central methodological challenge in this field is that theoretical predictions are difficult to test empirically without either proprietary exchange data---which is expensive, restricted, and often unavailable to academic researchers---or a simulation environment that faithfully reproduces the relevant institutional mechanics.

The ideal simulation platform for microstructure research must satisfy four requirements simultaneously. First, it must be \emph{fast}: parameter sweeps across hundreds or thousands of seeds must complete in minutes rather than days, enabling the kind of systematic sensitivity analysis that characterises rigorous empirical work. Second, it must be \emph{deterministic}: the same event stream and seed must produce bit-for-bit identical trades, fills, and book evolution, so that results are exactly reproducible by other researchers. Third, it must implement the full \emph{institutional complexity} of a modern continuous auction---including multiple order types, time-in-force instructions, market phases, and a configurable circuit-breaker mechanism---so that findings are less likely to be artefacts of an exchange model that lacks phase structure entirely. Fourth, it must expose a comfortable \emph{Python interface} so that researchers can write strategies and analyse results using the scientific Python ecosystem without engaging with C++ internals.

\subsection{The Gap in Existing Tools}



To our knowledge, MSIM is unusual in combining sub-50\,ns matching-kernel latency, a pybind11 Python research interface, deterministic reproducibility, and an integrated stylised-fact measurement layer in a compact open-source C++20 codebase; a systematic comparison across the full simulator landscape is deferred to future work. ABIDES \citep{byrd2020} prioritises explicit network-packet modelling and behavioural realism---a deliberate architectural trade-off that is inherently more expensive than a deterministic latency-buffer approach and better suited to studies requiring fine-grained per-agent network topology modelling. General-purpose agent-based modelling frameworks such as Mesa \citep{masad2015} provide no matching engine. Commercial platforms require proprietary licences.

MSIM has no live market connectivity and is intended exclusively for academic research and education. MSIM is designed to close this gap. Its core matching engine is implemented in C++20 with careful attention to cache efficiency and allocation-free critical paths. The simulation layer adds agents, a structured run loop, transaction cost analysis (TCA), and a stylised facts measurement module. A pybind11 bridge exposes the full engine to Python. The engine is validated against five stylised facts simultaneously in a single multi-agent validation run.

\subsection{Contributions}

The contributions of this paper are as follows:

\begin{enumerate}[leftmargin=*, label=\arabic*.]
\item A complete description of the MSIM architecture and the design choices that enable sub-50 ns market order processing, including formal descriptions of the core data structures (\Cref{sec:architecture,sec:datastructures}).

\item A systematic account of eight optimisation passes, each with root-cause analysis, quantified before-and-after measurements, and complexity analysis (\Cref{sec:optimisation}).

\item The full mathematical foundations of all agent models---Hawkes noise trader, Avellaneda-Stoikov market maker, mean-reversion informed value agent (inspired by Glosten-Milgrom), MACD (Moving Average Convergence/Divergence) momentum agent, and Almgren-Chriss implementation shortfall agent---and of the stylised facts measurement layer (\Cref{sec:math}).

\item An empirical validation demonstrating qualitative emergence of five canonical microstructure stylised facts, with four showing strong statistical consistency with primary empirical literature and one (price impact) emergent but not formally significant at the current sample size (\Cref{sec:validation}).

\item An open-source release under the MIT licence at \url{https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-}, including all benchmark scripts, plotting tools, and the Python package.
\end{enumerate}

The primary contribution is the integrated platform: sub-50\,ns matching performance enables the kind of large-scale, multi-seed stylised facts studies that are impractical in slower simulation environments.

\subsection{Paper Organisation}

The remainder of this paper is organised as follows. \Cref{sec:related} surveys related work in simulation platforms, exchange infrastructure, and the empirical literature that motivates the agent designs. \Cref{sec:architecture} describes the four-layer system architecture. \Cref{sec:datastructures} presents the core data structures and their algorithmic complexity. \Cref{sec:math} develops the mathematical foundations of all agent models and the stylised facts estimators. \Cref{sec:optimisation} documents the eight optimisation passes. \Cref{sec:validation} reports the empirical validation. \Cref{sec:perf} presents the complete benchmark suite. \Cref{sec:conclusion} concludes with limitations and directions for future work. \Cref{app:benchmarks,app:api} provide supplementary benchmark tables and API (application programming interface) reference.

% ─────────────────────────────────────────────────────────────────────────────
\section{Related Work}
\label{sec:related}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Academic Simulation Platforms}

\paragraph{ABIDES.} The Agent-Based Interactive Discrete Event Simulation of \citet{byrd2020} is the most complete academic microstructure simulator currently available. ABIDES models discrete-event agent interactions with explicit network latency, kernel-driven message passing between agents, and a sophisticated agent library including zero-intelligence traders, momentum agents, and adaptive market makers. ABIDES has been used to study flash crashes, market impact, and the sensitivity of spread dynamics to latency heterogeneity \citep{byrd2019}. Its primary limitation for the use case we address is throughput: the Python implementation is optimised for behavioural realism rather than raw throughput. A direct numerical throughput comparison between MSIM and ABIDES is not meaningful given the architectural difference: ABIDES measures end-to-end event processing including network and agent logic, while MSIM's kernel throughput figure excludes agent computation. The appropriate benchmark is the Phase~0$\to$Phase~8 progression within MSIM itself (\Cref{tab:optresults}), which demonstrates 5--25$\times$ improvement on identical operations.

\paragraph{Mesa.} Mesa \citep{masad2015} is a general-purpose Python agent-based modelling framework widely used in social simulation. Several market microstructure studies have built limit order book simulations on top of Mesa. While its modular design is convenient for experimentation, Mesa provides no matching engine, no financial primitives, and no performance optimisation; all LOB mechanics must be implemented from scratch, typically without the invariant tests that a dedicated exchange system requires.

\paragraph{Bristol Stock Exchange.} The Bristol Stock Exchange \citep{cliff2018} is a Python-based continuous double-auction simulator widely used in agent-based teaching and research. Like Mesa-based approaches, its Python implementation constrains throughput relative to MSIM's C++20 kernel. Several open-source C++ limit order book implementations exist on public repositories but do not publish standardised latency benchmarks against a common hardware baseline, making direct quantitative comparison difficult. Hardware-accelerated matching engines using FPGAs (field-programmable gate arrays) achieve sub-10\,ns latencies in production settings but operate at a different level of the software stack and are outside the scope of a software research simulator.

\paragraph{Other simulators.} The Zilico market simulator \citep{vach2015}, the Santa Fe Artificial Stock Market \citep{lebaron1999}, and several bespoke implementations in the academic literature provide varying levels of institutional detail. None combine sub-microsecond matching with a full Python research interface and automated stylised facts validation.

\subsection{Exchange Infrastructure}

\paragraph{LMAX Disruptor.} \citet{thompson2011} describe the LMAX (London Multi-Asset Exchange) Disruptor, a ring-buffer-based inter-thread communication pattern that achieves sub-microsecond order routing latency in production. The Disruptor eliminates lock contention through carefully managed cache-line ownership. MSIM borrows the architectural insight that contiguous memory layout is the dominant performance determinant in financial systems, but does not implement the Disruptor pattern itself since MSIM operates deterministically on a single thread.

\paragraph{FIX and ITCH protocols.} FIX (Financial Information eXchange) and ITCH (a binary drop-copy protocol used by NASDAQ and others) are the dominant wire protocols for exchange connectivity. Real exchange matching engines communicate via FIX (Financial Information eXchange) for order management and ITCH for market data dissemination. MSIM does not implement network protocols but the internal `Order` struct is designed to be field-compatible with FIX order representations, facilitating future integration.

\subsection{Empirical LOB Literature}

\paragraph{LOBster.} \citet{huang2011} provide a reconstruction of the NASDAQ limit order book from ITCH message data in a standardised CSV format. LOBster has become the standard empirical benchmark for validating simulation output against real market data. The stylised facts we report in \Cref{sec:validation} are benchmarked against the stylised-facts literature, principally \citet{cont2001} and \citet{bouchaud2004}. For a comprehensive treatment of high-frequency econometrics see \citet{aitsahalia2014}. LOBster \citep{huang2011} provides a later (2011) empirical data source that future work could use for direct parameter calibration.

\paragraph{Stylised facts.} The concept of stylised facts---statistical regularities that appear robustly across assets, markets, and time periods---was formalised by \citet{cont2001}, who surveys evidence for fat-tailed returns, volatility clustering, absence of autocorrelation in raw returns, and long-range dependence in volatility measures. Our validation targets this set. The trade-sign autocorrelation we measure is documented by \citet{bouchaud2004}, who attribute it to strategic order splitting by institutional investors. The bid-ask bounce autocorrelation derives from the Roll (1984) model. Spread decomposition follows \citet{huang1997} and \citet{glosten1988}.

\paragraph{Market impact.} The square-root law of market impact---$\Delta p \propto |Q|^{0.5}$---is documented by \citet{almgren2005} and \citet{kyle1985}. The power-law exponent $\delta \approx 0.5$ serves as a secondary validation criterion in our simulation output.

% ─────────────────────────────────────────────────────────────────────────────
\section{System Architecture}
\label{sec:architecture}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Design Philosophy}

MSIM is organised around the principle of strict layer separation. No layer has any upward dependency: the matching engine has no knowledge of agents; the rule layer has no knowledge of the run loop; the measurement layer has no influence on the simulation. This separation provides three practical benefits. First, the exchange kernel can be tested in complete isolation, enabling strong invariant-based testing of FIFO ordering, FOK (Fill-or-Kill) atomicity, and book correctness---and supporting formal reasoning about these properties if desired. Second, the agent and measurement layers can evolve without destabilising the kernel. Third, the Python interface can expose each layer independently, allowing researchers to access raw book state, TCA summaries, or stylised facts results through a consistent API. The four-layer structure is illustrated in Figure~\ref{fig:architecture}.


\begin{figure}[htbp]
\centering
\begin{tikzpicture}[
  font=\sffamily\small,
  layer/.style 2 args={
    draw=#2, fill=#1, rounded corners=5pt,
    minimum width=11cm, minimum height=1.45cm,
    text width=10.8cm, align=center, line width=0.8pt
  },
  pybox/.style={
    draw=gray!55, fill=gray!6, rounded corners=5pt,
    minimum width=2.6cm, minimum height=5.2cm,
    text width=2.4cm, align=center, line width=0.8pt
  },
  farrow/.style={-{Stealth[length=5.5pt,width=4pt]}, line width=0.8pt},
]

%% ── Four layers ──────────────────────────────────────────────────────────
\node[layer={blue!10}{blue!55!black}] (L1) at (0, 0) {%
  \textbf{Layer 1 — Exchange Kernel}\\[3pt]
  {\footnotesize\texttt{OrderBook}\enspace\texttt{MatchingEngine}\enspace\texttt{SmallVector<Trade,4>}}\\[1pt]
  {\scriptsize\texttt{FlatPriceMap}\;$\cdot$\;\texttt{FlatHashMap}\;$\cdot$\;Robin Hood locator}%
};

\node[layer={orange!10}{orange!60!black}] (L2) at (0,-2.25) {%
  \textbf{Layer 2 — Venue Rules}\\[3pt]
  {\footnotesize\texttt{RuleSet}\enspace\texttt{RejectReason}\enspace\texttt{MarketPhase}}\\[1pt]
  {\scriptsize Validity\;$\cdot$\;STP\;$\cdot$\;Phase transitions\;$\cdot$\;Circuit breakers}%
};

\node[layer={green!9}{green!50!black}] (L3) at (0,-4.50) {%
  \textbf{Layer 3 — Simulation World}\\[3pt]
  {\footnotesize\texttt{World::run()}\enspace\texttt{IAgent}\enspace\texttt{LatencyActionBuffer}}\\[1pt]
  {\scriptsize Hawkes\;$\cdot$\;A-S\,MM\;$\cdot$\;FundValue\;$\cdot$\;Momentum\;$\cdot$\;VWAP\;$\cdot$\;TWAP\;$\cdot$\;IS}%
};

\node[layer={purple!9}{purple!55!black}] (L4) at (0,-6.75) {%
  \textbf{Layer 4 — Measurement}\\[3pt]
  {\footnotesize\texttt{StylisedFactsMeasurer}\enspace\texttt{TCA}\enspace\texttt{PnL series}}\\[1pt]
  {\scriptsize 5 stylised facts\;$\cdot$\;Fill records\;$\cdot$\;Per-agent slippage}%
};

%% ── Output arrows above Layer 1 ─────────────────────────────────────────
\draw[farrow, blue!60!black]
  ([xshift=1.2cm]L1.north) -- ++(0, 0.6)
  node[above, font=\footnotesize, text=blue!70!black] {\texttt{Trade}};
\draw[farrow, blue!60!black]
  ([xshift=-1.2cm]L1.north) -- ++(0, 0.6)
  node[above, font=\footnotesize, text=blue!70!black] {\texttt{MatchResult}};

%% ── Inter-layer arrows (left-aligned to avoid overlap with box text) ─────
\draw[farrow]
  ([xshift=-1.5cm]L2.north) -- ([xshift=-1.5cm]L1.south)
  node[midway, left, font=\footnotesize, xshift=-3pt] {\texttt{Order}};

\draw[farrow]
  ([xshift=-1.5cm]L3.north) -- ([xshift=-1.5cm]L2.south)
  node[midway, left, font=\footnotesize, xshift=-3pt] {submit/cancel};

\draw[farrow, purple!60!black]
  ([xshift=-1.5cm]L4.north) -- ([xshift=-1.5cm]L3.south)
  node[midway, left, font=\footnotesize, xshift=-3pt, text=purple!70!black] {observe};

%% ── Python Bridge ────────────────────────────────────────────────────────
\node[pybox] (PY) at (7.6, -3.375) {%
  \textbf{Python}\\[2pt]\textbf{Bridge}\\[8pt]
  {\footnotesize\texttt{pybind11}}\\[5pt]
  {\scriptsize\texttt{step()} calls\\[2pt]GIL acquire\\[2pt]DataFrames\\[2pt]\texttt{result.sf}}%
};
\draw[{Stealth[length=4pt]}-{Stealth[length=4pt]}, gray!60, line width=0.7pt, dashed]
  (PY.west) -- ([xshift=4pt]L3.east)
  node[midway, above, font=\scriptsize, text=gray!70] {actions};
\node[font=\scriptsize, text=gray!70] at (5.85, -4.8) {/\,state};

%% ── No-upward-dependency brace ────────────────────────────────────────────
\draw[decorate,
      decoration={brace, amplitude=5pt, mirror},
      gray!45, line width=0.6pt]
  (-6.2, 0.72) -- (-6.2, -7.47)
  node[midway, left, xshift=-7pt, font=\scriptsize\itshape,
       text=gray!65, align=right, text width=1.5cm] {no upward\\dependency};

%% ── Performance callout ───────────────────────────────────────────────────
\node[draw=blue!35, fill=blue!5, rounded corners=3pt, inner sep=5pt,
      font=\footnotesize, align=center, text width=2.6cm] at (-4.0, 0) {
  \textbf{46.6\,ns} p50\\21.9\,M\,ops/s
};
\draw[->, gray!50, dashed, line width=0.5pt] (-2.7, 0) -- (-5.5, 0);

\end{tikzpicture}
\caption{%
  MSIM four-layer architecture. Each layer has a well-defined interface and no
  upward dependency---changes to the matching engine (Layer~1) cannot propagate to the
  measurement layer (Layer~4) without passing through the world interface (Layer~3).
  The Python bridge exposes the full C++ type system via \texttt{pybind11}.%
}
\label{fig:architecture}
\end{figure}

\subsection{Layer 1: Exchange Kernel}

The exchange kernel consists of two classes: \texttt{OrderBook} and \texttt{MatchingEngine}.

\texttt{OrderBook} is responsible for the state of all resting limit orders. It exposes five operations with defined complexity guarantees:
\begin{itemize}[noitemsep]
    \item \texttt{add\_resting\_limit(Order)}: $O(\log N_p)$ for an existing price level (binary search); $O(\log N_p + N_p)$ worst-case when a new price level is created, due to the vector shift in \texttt{FlatPriceMap::operator[]}. In practice, new levels are created infrequently relative to additions at existing levels. The flat p50 latency of \texttt{BM\_BookAddRestingLimit} across book sizes ($N = 100$ to $N = 10{,}000$) confirms this empirically: if new-level creation were frequent, the $O(N_p)$ shift cost would produce a visible upward trend in latency with book depth, which is not observed.
    \item \texttt{cancel(OrderId)}: $O(1)$ expected (hash map lookup + tombstone write).
    \item \texttt{modify\_qty(OrderId, Qty)}: $O(1)$ expected (hash map lookup + in-place update).
    \item \texttt{depth(Side, N)}: $O(N)$ (iterate top-$N$ price levels).
    \item \texttt{queue\_info(OrderId)}: $O(K)$ where $K$ is the queue length at the queried price level.
\end{itemize}

\texttt{MatchingEngine} wraps \texttt{OrderBook} and adds the \texttt{process(Order)} and \texttt{flush(Ts)} interfaces (see Figure~\ref{fig:order_lifecycle} for the complete order decision flow). \texttt{process} routes an incoming order through the rule layer, dispatches to the appropriate matching path, and returns a \texttt{MatchResult} containing the generated trades, the final order status, and any rejection reason. \texttt{flush} processes timed events (e.g. deferred limit order expirations during auction phases) and returns early via a cached \texttt{next\_event\_ts\_} timestamp when no events are due.


\begin{figure}[htbp]
\centering
\begin{tikzpicture}[
  font=\small,
  terminator/.style={
    draw=gray!60, fill=gray!12, rounded corners=10pt,
    minimum width=3.2cm, minimum height=0.7cm, align=center, line width=0.7pt
  },
  process/.style={
    draw=#1!70!black, fill=#1!12, rounded corners=3pt,
    minimum width=3.4cm, minimum height=0.85cm, align=center,
    text width=3.2cm, line width=0.7pt
  },
  decision/.style={
    draw=orange!65!black, fill=orange!10, diamond, aspect=2.4,
    minimum width=3.6cm, minimum height=0.9cm, align=center,
    inner sep=2pt, line width=0.7pt
  },
  reject/.style={
    draw=red!60!black, fill=red!8, rounded corners=3pt,
    minimum width=2.8cm, minimum height=0.85cm, align=center,
    text width=2.6cm, line width=0.7pt
  },
  annot/.style={
    draw=blue!30, fill=blue!5, rounded corners=3pt, inner sep=4pt,
    font=\scriptsize, align=center, text width=2.2cm, line width=0.6pt
  },
  farrow/.style={-{Stealth[length=5pt,width=3.5pt]}, line width=0.75pt},
  label/.style={font=\footnotesize, midway},
]

%% ── Nodes (top to bottom, centred at x=0) ────────────────────────────────
\node[terminator]            (start)  at (0, 0)     {\textbf{Order} submitted};
\node[decision]              (rule)   at (0,-1.8)   {\texttt{RuleSet::pre\_accept()}};
\node[reject]                (rej)    at (4.2,-1.8) {\textbf{Reject}\\{\scriptsize\texttt{RejectReason} code}};
\node[decision]              (type)   at (0,-3.6)   {Order type?};
%% Market path (left)
\node[process=blue]          (chkliq) at (-3.5,-3.6) {\texttt{available\_liquidity()}\\{\scriptsize FOK pre-check}};
\node[decision]              (fok)    at (-3.5,-5.4) {FOK and\\insuff.\ liq.?};
\node[reject]                (fokrej) at (-3.5,-7.0) {\textbf{Return unfilled}};
\node[process=blue]          (matchm) at (-3.5,-8.6) {\texttt{match\_buy /}\\{\texttt{match\_sell()}}};
%% Limit path (right)
\node[decision]              (cross)  at (3.5,-5.0) {Crosses\\best price?};
\node[process=blue]          (matchl) at (3.5,-7.0) {\texttt{match\_buy /}\\{\texttt{match\_sell()}}\\{\scriptsize IOC: partial OK}};
\node[process=yellow!50!gray] (rest)  at (3.5,-8.6) {\texttt{add\_resting\_limit()}\\{\scriptsize insert in FlatPriceMap}};
%% Convergence
\node[process=green!60!black] (sv)   at (0,-10.4) {\textbf{\texttt{SmallVector<Trade,\,4>}}\\{\scriptsize inline; heap only if ${>}4$ fills}};
\node[terminator]             (result)at (0,-12.0)  {\textbf{MatchResult} returned};

%% ── Arrows ───────────────────────────────────────────────────────────────
\draw[farrow] (start) -- (rule);
\draw[farrow, red!60!black] (rule) -- (rej) node[label, above] {fail};
\draw[farrow] (rule) -- (type) node[label, right, xshift=2pt] {pass};
\draw[farrow] (type) -- (chkliq) node[label, above] {Market};
\draw[farrow] (type) -- (cross)  node[label, above] {Limit};
\draw[farrow] (chkliq) -- (fok);
\draw[farrow, red!60!black] (fok) -- (fokrej) node[label, left, xshift=-2pt] {yes};
\draw[farrow] (fok.south) -- ++(0,-0.3) -| (matchm.north)
  node[pos=0.2, above, font=\footnotesize] {no};
\draw[farrow] (cross) -- (matchl) node[label, right, xshift=2pt] {yes};
\draw[farrow] (cross.south) -- ++(0,-0.4) -| (rest.north)
  node[pos=0.2, above, font=\footnotesize] {no};
%% Convergence arrows to SmallVector
\draw[farrow] (matchm.south) -- ++(0,-0.35) -| (sv.west);
\draw[farrow] (matchl.south) -- ++(0,-0.2) -| (sv.east);
\draw[farrow] (rest.south)   -- ++(0,-0.35) -| (sv.east);
\draw[farrow] (sv) -- (result);

%% ── Latency annotations ──────────────────────────────────────────────────
\node[annot] at (-6.8,-1.8)  {\textbf{14.9\,ns}\\fast-path\\reject};
\draw[->, gray!50, dashed, line width=0.5pt] (-5.6,-1.8) -- (rej.west);

\node[annot] at (6.8,-8.6)   {\textbf{150.2\,ns}\\add resting\\limit};
\draw[->, gray!50, dashed, line width=0.5pt] (5.6,-8.6) -- (rest.east);

\node[annot] at (0,-13.3)    {\textbf{46.6\,ns} p50\\market order\\(0\,allocs)};

\end{tikzpicture}
\caption{%
  Order lifecycle and hot-path decision flow. Incoming orders pass through
  \texttt{RuleSet} (Layer~2) before entering the matching engine (Layer~1).
  Market orders call \texttt{available\_liquidity()} only for FOK orders;
  all others proceed directly to \texttt{match\_buy}/\texttt{match\_sell}.
  Limit orders either cross immediately or rest in the \texttt{FlatPriceMap}.
  Trade objects are stored in a \texttt{SmallVector<Trade,4>} inline buffer,
  allocating no heap memory for ${\leq}\,4$ simultaneous fills.
  Benchmark latencies (p50, Release, MSVC 19.44 (Microsoft Visual C++)) are annotated.%
}
\label{fig:order_lifecycle}
\end{figure}

\subsection{Layer 2: Rule and Policy Layer}

\texttt{RuleSet::pre\_accept()} performs pre-match admission checks independently of the matching engine:

\begin{itemize}[noitemsep]
    \item \textbf{Halt enforcement:} orders arriving during a market halt are queued for the reopening auction (configurable) or rejected with \texttt{RejectReason::MarketHalted}.
    \item \textbf{Tick-size validation (limit orders only):} the price of a limit order must lie on the configured tick grid; violated orders are rejected with \texttt{RejectReason::PriceNotOnTick}.
    \item \textbf{Minimum quantity and lot size:} orders below the minimum quantity are rejected with \texttt{RejectReason::QtyBelowMinimum}; orders whose quantity is not a whole multiple of the lot size are rejected with \texttt{RejectReason::QtyNotOnLot}.
\end{itemize}

Three further mechanisms are applied outside \texttt{pre\_accept}:

\begin{itemize}[noitemsep]
    \item \textbf{Self-trade prevention:} enforced inside \texttt{match\_buy}/\texttt{match\_sell} in the matching engine, with configurable modes \texttt{None}, \texttt{CancelTaker}, and \texttt{CancelMaker}.
    \item \textbf{Price bands (volatility interruption):} checked in the matching engine before execution. An order whose execution price would breach the configured collar does not generate a typed reject; instead, the engine transitions the venue into \texttt{MarketPhase::Auction}, sets an auction end timestamp, and queues the incoming order for uncrossing.
    \item \textbf{Circuit breaker:} implemented in \texttt{MatchingEngine::maybe\_trigger\_circuit\_breaker()}. The trigger compares the last executed trade price against a stored reference price \texttt{cb\_ref\_price\_}; if the trade price falls below \texttt{cb\_ref\_price\_\,×\,(1 - cb\_drop\_bps\,/\,10000)}, the engine halts the market and schedules a reopening auction. This is a downward reference-price threshold mechanism, not a rolling-window mid-price comparison.
\end{itemize}

Pre-accept rejections return a typed \texttt{RejectReason} enum value (\texttt{MarketHalted}, \texttt{PriceNotOnTick}, \texttt{QtyBelowMinimum}, \texttt{QtyNotOnLot}, and others---STP currently cancels the aggressor or maker by zeroing quantity rather than setting a typed reject code). FOK orders with insufficient liquidity return an empty \texttt{MatchResult} (zero fills) without a typed reject code---the caller distinguishes this from a policy reject by checking fill count.

\subsection{Layer 3: Simulation World}

The \texttt{World} class owns the agent collection, the matching engine, and the run loop. \texttt{World::run(seed, horizon, config)} is the simulation entry point. It performs the following initialisation before entering the main loop:

\begin{enumerate}[noitemsep]
    \item Seed each agent using splitmix64 \citep{steele2014,steele2021} with position-dependent salt: $s_i = \text{splitmix64}(\text{seed} \oplus (i+1))$, ensuring each agent receives a statistically independent stream. splitmix64 was chosen for its excellent spectral quality, zero-overhead implementation, and well-documented inter-stream independence properties.
    \item Construct per-agent \texttt{LatencySampler} objects from the configured \texttt{LatencyDistConfig} (FIXED, GAUSSIAN, LOG\_NORMAL, or UNIFORM).
    \item Pre-reserve all output vectors and hash maps from the expected step count, eliminating reallocation during the run.
    \item Pre-reserve the \texttt{StylisedFactsMeasurer} internal accumulators.
\end{enumerate}

The main loop executes seven steps per tick (labelled A--G in \Cref{alg:runloop}).

\begin{algorithm}[H]
\caption{MSIM Main Simulation Loop}
\label{alg:runloop}
\begin{algorithmic}[1]
\For{$t_s = 0, \Delta t, 2\Delta t, \ldots, T_{end}$}
    \State \textbf{A.} $\text{flushed} \leftarrow \text{engine.flush}(t_s)$
        \Comment{process timed events; fast-path if $t_s < \text{next\_event\_ts}$}
    \If{$\text{flushed} \neq \emptyset$}
        \State update accounts; emit stylised facts records
    \EndIf
    \State \textbf{B.} $b^* \leftarrow \text{book.best\_bid}()$;\quad $a^* \leftarrow \text{book.best\_ask}()$;\quad $m \leftarrow (b^*+a^*)/2$
        \Comment{computed once; reused in steps F, G}
    \State \textbf{C.} $\text{sfm.add\_top}(t_s, b^*, a^*, m)$
    \State \textbf{D.} $\text{lat\_buf.clear}()$
    \For{each agent $i$}
        \State $\mathbf{a}_i \leftarrow \text{agent}_i.\text{step}(t_s, \text{view}, \text{state}_i)$
        \If{latency enabled}
            \State $\delta_i \leftarrow \text{sampler}_i.\text{sample}()$;\quad enqueue $(\mathbf{a}_i, t_s + \delta_i, i)$ in lat\_buf
        \Else
            \State dispatch $\mathbf{a}_i$ immediately
        \EndIf
    \EndFor
    \State \textbf{E.} insertion-sort lat\_buf by effective\_ts; dispatch all actions
    \State \textbf{F.} $\text{out.tops.push}(t_s, b^*, a^*, m)$
        \Comment{reuse $b^*, a^*, m$ from step B}
    \If{record\_pnl\_series}
        \State \textbf{G.} for each agent: record $(t_s, q_i, X_i, m)$ using $m$ from step B
    \EndIf
\EndFor
\end{algorithmic}
\end{algorithm}

\subsection{Layer 4: Measurement}

The measurement layer comprises three passive components that observe but do not influence the simulation:

\textbf{StylisedFactsMeasurer} accumulates \texttt{TradeRecord} and \texttt{TopRecord} entries during the run and computes all five stylised fact statistics after \texttt{run()} returns. All accumulation vectors are pre-reserved to \texttt{n\_steps} and \texttt{est\_fills} at construction time, eliminating $\log_2(n\_steps)$ reallocation events.

\textbf{TCA module} records per-fill data (arrival mid-price, fill price, side, is-maker flag) and computes per-agent summaries including limit fill rate, average slippage, turnover notional, and final mark-to-market PnL.

\textbf{PnL series} records the mark-to-market position at every simulation step: $\text{mtm\_pnl}_t^{(i)} = X_t^{(i)} + q_t^{(i)} \cdot m_t$, where $X_t^{(i)}$ is agent $i$'s cash in ticks and $q_t^{(i)}$ is their net position.

\subsection{Python Interface}

The pybind11 bridge exposes all C++ types with the following design choices:

\begin{itemize}[noitemsep]
    \item \texttt{std::optional<Price>} maps to \texttt{int | None} in Python, preserving the distinction between ``no quote'' and ``quote at price 0''.
    \item \texttt{std::span<const Trade>} and \texttt{SmallVector<Trade,4>} both convert implicitly to Python lists via the pybind11 span type caster.
    \item The \texttt{IAgent} trampoline acquires the Python GIL on each \texttt{step()} call, allowing C++ and Python agents to coexist in the same world.
    \item \texttt{add\_agent} takes a raw \texttt{IAgent*} pointer with \texttt{py::keep\_alive<1,2>} rather than \texttt{unique\_ptr<IAgent>}. While newer pybind11 features (\texttt{py::smart\_holder}) do support transferring Python trampoline objects via \texttt{unique\_ptr}, the raw pointer approach with \texttt{keep\_alive} is simpler and sufficient for the current binding design.
\end{itemize}

The compiled extension module (\texttt{\_msim\_core.*.pyd} on Windows, \texttt{.so} on Unix) is written directly into \texttt{src/python/msim/} by the CMake build system, making \texttt{import msim} functional immediately after \texttt{cmake --build} with no pip installation required.

% ─────────────────────────────────────────────────────────────────────────────
\section{Core Data Structures}
\label{sec:datastructures}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{FlatPriceMap}

\begin{definition}
A \emph{FlatPriceMap} is a price-indexed map $\mathcal{M}: \text{Price} \to \text{Level}$ implemented as a sorted \texttt{std::vector<Entry>} with a front offset cursor $f$ and a comparator $\prec$ (either $>$ for bids or $<$ for asks).
\end{definition}

The fundamental insight is that a typical limit order book has $N_p \ll 10^3$ distinct active price levels at any time (empirically 10--200 for liquid instruments). A sorted vector of this size fits entirely within the L1 data cache (48 KB per core on the test machine). Binary search over a 200-element contiguous array requires approximately $\lceil \log_2 200 \rceil = 8$ comparisons, each accessing sequential cache lines. A red-black tree with the same 200 elements requires 8 pointer dereferences into non-contiguous heap-allocated nodes; each dereference is a potential L1 cache miss (${\approx}4$\,ns) or L2 miss (${\approx}12$\,ns) \citep{fog2024,intel2024}.

The \texttt{front\_offset} field addresses the sweep performance problem. When the matching engine fully consumes a price level, \texttt{erase(begin())} is called. On a vanilla \texttt{std::vector}, this is an $O(N_p)$ memmove. With \texttt{front\_offset}:

\begin{equation}
\text{erase}(\text{begin}()) \equiv \texttt{front\_offset} \mathrel{+}= 1 \quad \Rightarrow \quad O(1)
\end{equation}

A lazy compaction restores contiguity when $\texttt{front\_offset} > |\mathcal{M}| / 2$:

\begin{equation}
\text{compact}() \equiv \text{erase}([\texttt{entries.begin}(), \texttt{entries.begin}() + \texttt{front\_offset}]) \quad O(N_p)
\end{equation}

Front erases are $O(1)$. Occasional compactions cost $O(n)$ to move the live suffix, but over a long front-sweep the total compaction work is amortised linear in the number of removed levels---empirically confirmed by the flat p50 latency across $K=1$ to $K=1024$ in \Cref{app:benchmarks}. This resolved the 4$\times$ Windows regression on \texttt{BM\_ProcessMarket\_SweepKLevels}.

\subsection{FlatHashMap}

\begin{definition}
A \emph{FlatHashMap} is an open-addressing hash map with Robin Hood probing implemented as a contiguous \texttt{std::vector<Slot>} where each slot stores a key $k$, value $v$, and probe distance $d \in \{0, 1, 2, \ldots\}$. The sentinel $d = 0$ indicates an empty slot.
\end{definition}

\paragraph{Fibonacci hashing.} Keys are mapped to slot indices via:
\begin{equation}
h(k) = \left\lfloor \frac{k \cdot \varphi^{-1} \cdot 2^{64}}{2^{64 - \log_2 C}} \right\rfloor
\end{equation}
where $C$ is the table capacity (a power of two) and $\varphi^{-1} \cdot 2^{64} \approx 11400714819323198485$ is the Fibonacci hashing constant (multiplication is performed modulo $2^{64}$, i.e.\ unsigned 64-bit wrap-around). This constant was chosen because it maps sequential \texttt{uint64\_t} keys (as generated by MSIM's OrderId scheme) uniformly across the table, avoiding the clustering that affects modular hashing with sequential inputs.

\paragraph{Robin Hood insertion.} When probing for an empty slot to insert key $k$ with distance $d$, if the current occupant has distance $d' < d$ (i.e., it is ``richer'' -- closer to its natural position), the incoming element displaces the occupant and continues with the displaced element. This \emph{Robin Hood stealing} property ensures that probe distances are bounded and approximately equal across all occupied slots. \citet{celis1985} prove that Robin Hood hashing achieves a probe-length variance of $O(1)$ independent of load factor---a substantially stronger guarantee than standard linear probing, where variance grows as $O(1/(1-\alpha)^2)$. At $\alpha = 0.5$ (MSIM's default), empirical measurements confirm $\mathbb{E}[\text{probe}] \approx 1.5$ slots.

\paragraph{Early termination in find.} The Robin Hood invariant implies that if $\texttt{slots}[i].d < d$, then key $k$ cannot be present at position $i$ or any subsequent position: any element with distance less than $d$ would have been displaced to an earlier position by Robin Hood insertion. Therefore \texttt{find} can stop at the first slot with $d' < d$, rather than probing until an empty slot:

\begin{equation}
\texttt{find}(k)\colon\quad\text{probe until }\texttt{slots}[i].d < d_{\mathrm{cur}}\text{ or }\texttt{slots}[i].k = k
\end{equation}

\paragraph{Backward-shift deletion.} After erasing slot $i$, slots to the right with probe distance $d' > 1$ (displaced from their natural position) are shifted left by one, decrementing their distance. This restores the probe-chain invariant without tombstones:

\begin{equation}
\begin{aligned}
\texttt{erase\_at}(i)\colon \quad
  &\texttt{slots}[i].d \leftarrow 0; \quad j \leftarrow i{+}1;\\
  &\textbf{while}\ \texttt{slots}[j].d > 1\colon\;
    \texttt{slots}[i] \leftarrow \texttt{slots}[j];\;
    \texttt{slots}[i].d \mathrel{-}= 1;\;
    \texttt{slots}[j].d \leftarrow 0;\;
    i \leftarrow j;\; j \mathrel{+}= 1
\end{aligned}
\end{equation}

The $d > 1$ condition means slots at their natural position ($d = 1$) are never moved, terminating the backward-shift quickly. Combined with Robin Hood's bounded probe distances, the expected number of slots moved per deletion is approximately 0--2 at $\alpha = 0.5$.

\paragraph{extract() for cancel.} The \texttt{cancel} operation needs to both find and erase a locator. The \texttt{extract(k, out)} method combines these in a single probe pass, avoiding the double hashing that would result from \texttt{find} followed by \texttt{erase}:

\begin{equation}
\texttt{extract}(k,\,\text{out})\colon\quad\text{probe until }k\text{ found};\;\text{out}\leftarrow\texttt{slots}[i].v;\;\texttt{erase\_at}(i);\;\text{return\,true}
\end{equation}

\paragraph{insert\_new() for add.} Since \texttt{OrderId} is never duplicated in the book (each submitted order has a unique identifier), \texttt{add\_resting\_limit} can use \texttt{insert\_new(k, v)}, which skips the equality check during probing. This saves one comparison per probed slot --- a small but measurable benefit at high insertion rates.

\subsection{SmallVector}

\texttt{SmallVector<T, N>} provides inline storage for $N$ elements and falls back to heap allocation for larger sequences. The critical insight is that MSIM's \texttt{MatchResult::trades} almost always contains 0--4 trades (a single market order hitting one to four resting orders). With $N = 4$, 99.93\% of processing calls never touch the heap (measured across 10 seeds: 9{,}763 of 9{,}770 fill-events generated $\leq 4$ simultaneous trades):

\begin{equation}
P(\text{heap allocated}) = P(|\text{trades}| > 4) = 0.00072 \quad \text{(measured: 7/9770 calls across 10 seeds)}
\end{equation}

The inline buffer is stored directly in the \texttt{MatchResult} struct, which lives on the call stack frame of \texttt{engine.process()}. No allocation at all occurs for the common case.

\subsection{Level Queue and Tombstone Design}

Each price level maintains:
\begin{align}
\texttt{Level} = \{&\; q: \texttt{vector<Order>}, \notag\\
                   &\; f: \texttt{size\_t} \text{ (front offset)}, \notag\\
                   &\; Q: \texttt{Qty} \text{ (total live quantity)}, \notag\\
                   &\; L: \texttt{uint32\_t} \text{ (live order count)}\}
\end{align}

Orders are appended to $q$ on insertion. Cancellation writes a tombstone ($\texttt{qty} \leftarrow 0$) at the absolute index stored in the locator. Matching advances $f$ past tombstones and increments it on full consumption. The live count $L$ is maintained incrementally: $L \mathrel{+}= 1$ on add, $L \mathrel{-}= 1$ on cancel and on full consumption during matching.

The depth query reads $L$ directly: $O(1)$ per level regardless of the number of cancelled orders. Without $L$, depth would require iterating $q$ from $f$ to $|q|$ counting non-zero entries---an $O(|q|)$ scan that grows linearly with the number of historical cancellations at that price.

% ─────────────────────────────────────────────────────────────────────────────
\section{Mathematical Foundations}
\label{sec:math}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Limit Order Book Mechanics}

\begin{definition}
At time $t$, the bid side $\mathcal{B}_t$ and ask side $\mathcal{A}_t$ of the limit order book are partially ordered sets of resting limit orders $o = (p, q, t_{\text{arr}}, \text{id}, \text{side})$ where $p$ is the limit price in ticks, $q > 0$ the remaining quantity, $t_{\text{arr}}$ the arrival timestamp, and id a unique identifier. Orders at the same price are ranked by $t_{\text{arr}}$ (FIFO). The best bid and ask are:
\begin{equation}
p^b_t = \max_{(p,\cdot) \in \mathcal{B}_t} p, \qquad p^a_t = \min_{(p,\cdot) \in \mathcal{A}_t} p
\end{equation}
with mid-price $m_t = (p^b_t + p^a_t)/2$ and bid-ask spread $s_t = p^a_t - p^b_t \geq 1$ tick.
\end{definition}

\paragraph{Market order execution.} An incoming buy market order of size $Q$ is matched against the ask side in price-time priority. Let $\mathcal{A}^{\leq p}_t = \{o \in \mathcal{A}_t : o.p \leq p\}$ for a limit price cap $p$. The total fillable quantity is:
\begin{equation}
\Phi(Q, p) = \min\!\left(Q,\, \sum_{o \in \mathcal{A}^{\leq p}_t} o.q\right)
\end{equation}
For a pure market order $p = \infty$; for an IOC limit order $p = p_{\text{limit}}$. The average execution price is the quantity-weighted mean of fill prices across all matched resting orders.

\paragraph{FOK atomicity.} A Fill-or-Kill order of size $Q$ is accepted if and only if $\Phi(Q, p) = Q$. The engine pre-checks this condition via \texttt{available\_liquidity()} before recording any trades. This guarantees that either the full quantity is filled in one atomic operation or no trades are recorded.

\begin{claim}[FOK Atomicity]
Let $\sigma$ be any sequence of FOK orders. Under MSIM's matching protocol, either all $Q$ lots of each FOK order are filled or zero lots are filled. No partial fills occur.
\end{claim}

\paragraph{Market-to-Limit.} A market-to-limit buy order executes at the current best ask $p^a_t$. Any unfilled remainder $Q - \Phi(Q, p^a_t)$ rests as a limit order at $p^a_t$, providing a price guarantee against adverse sweeps.

\paragraph{FIFO property.} Within each price level, MSIM maintains FIFO ordering by construction. Orders are appended to the queue vector; matching always starts from \texttt{front\_offset}; the locator stores an absolute index that never changes. Since the front-offset advances monotonically and indices are assigned at insertion, earlier orders always have smaller indices and are always matched first.

\subsection{Hawkes Self-Exciting Noise Trader}

The noise trader's arrival process is a marked Hawkes point process \citep{hawkes1971}. Let $N(t)$ be the counting process of order arrivals up to time $t$. The conditional intensity function is:

\begin{equation}
\lambda(t) = \mu + \psi(t), \qquad \psi(t) = \int_{-\infty}^{t} \alpha e^{-\beta(t-s)} \, dN(s)
\end{equation}

where $\mu > 0$ is the exogenous (baseline) rate, $\alpha \geq 0$ is the excitation amplitude (each arrival triggers additional arrivals), and $\beta > 0$ is the decay rate. The process is stationary when $\alpha / \beta < 1$ (subcritical), in which case the unconditional mean intensity is:

\begin{equation}
\bar{\lambda} = \frac{\mu}{1 - \alpha/\beta}
\end{equation}

In discrete simulation with step $\Delta t$, the excitement variable is updated via the exact exponential kernel recurrence:

\begin{equation}
\psi_{k+1} = \psi_k \cdot e^{-\beta \Delta t} + \alpha \cdot \mathbb{1}[\text{event at step }k]
\label{eq:hawkes_recurrence}
\end{equation}

An event at step $k$ occurs when a uniform sample $u \sim U[0,1]$ satisfies $u < 1 - e^{-\lambda_k \Delta t}$ (the probability of at least one arrival in $[k\Delta t, (k+1)\Delta t]$). This thinning approximation is accurate when $\lambda_k \Delta t \ll 1$. In the implementation, the indicator $\mathbb{1}[\text{last-trade update}]$ fires when \texttt{view.last\_trade} changes between steps; multiple fills at the same price may be collapsed into a single excitation event. For an illustrative low-intensity parameter set ($\mu = 0.1$, $\alpha = 0.05$, $\beta = 0.5$, $\Delta t = \SI{1}{\milli\second}$; the code defaults are $\mu=10$, $\alpha=5$, $\beta=20$ for a higher-activity regime, but the small-value set is used here to make the arithmetic transparent), the unconditional mean intensity is $\bar{\lambda} = 0.111$ events/s, giving $\bar{\lambda} \Delta t \approx 1.1 \times 10^{-4}$. Even at the theoretical maximum intensity $\lambda_{\max} \approx 0.211$ events/s, $\lambda_{\max} \Delta t \approx 2.1 \times 10^{-4} \ll 1$. The approximation error---$|P(\text{arrival}) - \lambda_k \Delta t| / \lambda_k \Delta t$---is below 0.011\% in all cases, confirming that the discrete-step thinning introduces negligible bias.

\paragraph{Order side bias.} The direction of each arrival is not uniformly random. Following \citet{cont2014}, the order side is biased by the current LOB imbalance. Let $q^b_t$ and $q^a_t$ be the quantities at the best bid and ask respectively. The imbalance is:

\begin{equation}
I_t = \frac{q^b_t - q^a_t}{q^b_t + q^a_t} \in [-1, 1]
\end{equation}

The probability that a triggered arrival is a buy order is:

\begin{equation}
P(\text{Buy} \mid I_t) = \frac{1}{2}(1 + \xi \cdot I_t)
\end{equation}

where $\xi \in [0,1]$ is the imbalance sensitivity (\texttt{imbalance\_bias} in the configuration). At $\xi = 0$ arrivals are symmetric; at $\xi = 1$ all arrivals follow the imbalance direction.

\paragraph{Order type selection.} Each triggered order is a market order with probability $p_{\text{mkt}}$ and a limit order with probability $1 - p_{\text{mkt}}$. Limit orders are placed at a price offset drawn uniformly from $[\texttt{min\_offset}, \texttt{max\_offset}]$ ticks from the best quote on the same side.

\subsection{Avellaneda-Stoikov Market Maker}

The market maker solves the continuous-time stochastic control problem of \citet{avellaneda2008}. Let $S_t$ be the mid-price following arithmetic Brownian motion:

\begin{equation}
dS_t = \sigma \, dW_t
\end{equation}

where $W_t$ is a standard Brownian motion and $\sigma > 0$ is instantaneous volatility. The market maker's inventory $q_t \in [-Q_{\max}, Q_{\max}]$ evolves when limit orders are filled. Cash $X_t$ increases by fill price on a sell and decreases on a buy. Terminal wealth is $W_T = X_T + q_T S_T$.

The market maker maximises the certainty-equivalent of terminal wealth under CARA (constant absolute risk aversion) utility with risk-aversion parameter $\gamma$:

\begin{equation}
\max_{\delta^b(t), \delta^a(t)} \; \mathbb{E}\!\left[-e^{-\gamma(X_T + q_T S_T)}\right]
\end{equation}

The fill rate of bid (ask) quotes at distance $\delta$ from the mid-price is modelled as a Poisson process with intensity $\lambda(\delta) = A e^{-\kappa \delta}$, where $\kappa$ is the fill-rate sensitivity (deeper quotes fill more slowly).

The HJB (Hamilton-Jacobi-Bellman) equation for this problem has the analytical solution:

\begin{equation}
r_t = S_t - q_t \cdot \gamma \cdot \hat{\sigma}_t^2 \cdot H_{\mathrm{roll}}
\label{eq:reservation}
\end{equation}

\Cref{eq:reservation} is the \emph{reservation price}: the price at which the market maker is indifferent between holding inventory and trading. Here $H_{\mathrm{roll}}$ is a fixed rolling horizon parameter (\texttt{cfg\_.T\_steps}) rather than a countdown to a terminal date; this is a rolling-horizon approximation to the finite-horizon A-S model that keeps the formula stationary across simulation steps. Inventory $q_t > 0$ (long) lowers the reservation price, causing the market maker to quote both sides lower, incentivising buyers to take from inventory.

The optimal bid-ask spread around the reservation price is:

\begin{equation}
\delta^* = \gamma \sigma^2 (T-t) + \frac{2}{\gamma} \ln\!\left(1 + \frac{\gamma}{\kappa}\right)
\label{eq:optspread}
\end{equation}

Following \citet{avellaneda2008}, $\delta^*$ is the total spread; the market maker quotes symmetrically at half-spread distance from the reservation price:

\begin{equation}
b_t = r_t - \frac{\delta^*}{2}, \qquad a_t = r_t + \frac{\delta^*}{2}
\label{eq:as_quotes}
\end{equation}

MSIM extends the reservation price with an imbalance adjustment:

\begin{equation}
r_t = S_t - q_t \cdot \gamma \cdot \sigma^2 \cdot (T-t) + \alpha_{\text{imb}} \cdot I_t \cdot \sigma
\label{eq:reservation_imb}
\end{equation}

where $I_t$ is the LOB imbalance and $\alpha_{\text{imb}}$ is the imbalance sensitivity. This extension is a heuristic departure from the analytical A-S solution: the modified reservation price \eqref{eq:reservation_imb} does not solve the original HJB equation. It captures the directional component of order flow observed empirically \citep{cont2014} but lacks the theoretical optimality guarantees of the unmodified A-S formula.

The half-spread is $\delta^*/2$, giving quotes:
\begin{align}
p^{\text{bid}}_t &= \max\!\left(r_t - \frac{\delta^*}{2},\; p^b_t - \delta_{\min}\right) \\
p^{\text{ask}}_t &= \min\!\left(r_t + \frac{\delta^*}{2},\; p^a_t + \delta_{\min}\right)
\end{align}
where $\delta_{\min}$ is the minimum half-spread enforced in ticks. Volatility is estimated online via EWMA (exponentially weighted moving average):

\begin{equation}
\hat{\sigma}^2_t = (1 - \alpha_{\text{ewma}}) \cdot \hat{\sigma}^2_{t-1} + \alpha_{\text{ewma}} \cdot \left(\Delta m_t\right)^2
\end{equation}

The warmup parameter \texttt{warmup} specifies the number of steps before the market maker begins quoting, allowing the EWMA to converge.

\subsection{Fundamental Value Agent}

The fundamental value agent is inspired by the informed trader of \citet{glosten1985}, but implements a \emph{mean-reversion informed} variant: a latent value $V_t$ is private information known to the agent but not to the market maker. $V_t$ follows a mean-reverting Ornstein-Uhlenbeck process anchored to $\mu$, a fixed reference level initialised from the first observed mid-price at simulation start. $\mu$ does not update during the run; the agent therefore mean-reverts to the initial mid-price, not to the current mid. $V_t$ evolves as:

\begin{equation}
dV_t = -\kappa (V_t - \mu) \, dt + \sigma_V \, dW^V_t
\label{eq:ou}
\end{equation}

where $\kappa > 0$ is the mean-reversion speed, $\sigma_V > 0$ is the fundamental value volatility, and $W^V_t$ is a Brownian motion independent of $W_t$ (the market-maker's price process). The anchor $\mu$ is set once at initialisation from the opening mid-price and remains fixed throughout the simulation. This makes the agent a private-signal mean-reversion trader rather than a fully informed trader in the Glosten-Milgrom sense.

The agent submits a market order when the gap between the fundamental value and the best executable quote exceeds a threshold $\theta > 0$, where $a_t$ is the best ask and $b_t$ is the best bid:

\begin{equation}
\text{action}_t = \begin{cases}
\text{Buy market order (qty = lot\_size)} & \text{if } V_t - a_t > \theta \\
\text{Sell market order (qty = lot\_size)} & \text{if } b_t - V_t > \theta \\
\text{Hold} & \text{otherwise}
\end{cases}
\label{eq:fv_rule}
\end{equation}

This trading rule exerts directional pressure when the private signal diverges from the market mid-price: repeated buys when $V > m + \theta$ push the mid-price upward, and repeated sells when $V < m - \theta$ push it downward, producing endogenous price discovery. The agent is not a fully informed Glosten-Milgrom trader; establishing that equilibrium would require an exogenous value process, not the fixed-anchor OU process implemented here.

\subsection{Momentum Agent}

The momentum agent implements a dual exponential moving average crossover strategy, a widely studied trend-following rule \citep{jegadeesh1993, murphy1999}. Let $\mu^f_t$ and $\mu^s_t$ denote the fast and slow EMAs (exponential moving averages) of the mid-price:

\begin{align}
\mu^f_t &= \alpha^f m_t + (1 - \alpha^f) \mu^f_{t-1} \\
\mu^s_t &= \alpha^s m_t + (1 - \alpha^s) \mu^s_{t-1}
\end{align}

where $\alpha^f > \alpha^s > 0$ (fast adapts more quickly). The MACD signal is:

\begin{equation}
z_t = \mu^f_t - \mu^s_t
\end{equation}

The agent maintains a target position $q^*_t$ bounded by $Q_{\max}$:

\begin{equation}
q^*_t = \begin{cases}
+\texttt{lot\_size} & \text{if } z_t > \delta_{\text{entry}} \\
-\texttt{lot\_size} & \text{if } z_t < -\delta_{\text{entry}} \\
0 & \text{if } |z_t| < \delta_{\text{exit}} \\
q^*_{t-1} & \text{otherwise (hold)}
\end{cases}
\label{eq:momentum}
\end{equation}

Position changes from $q^*_{t-1}$ to $q^*_t$ are executed via market orders. A warmup period \texttt{warmup\_steps} is observed before any trading begins.

\subsection{Almgren-Chriss Implementation Shortfall Agent}

The implementation shortfall (IS) agent executes the analytically optimal trajectory of \citet{almgren2001}. Given a parent order of $X$ lots to execute over $T$ steps, the agent minimises the mean-variance objective:

\begin{equation}
\min_{\{x_k\}_{k=0}^{T}} \; E[\text{IS}] + \lambda_{\text{IS}} \cdot \text{Var}[\text{IS}]
\label{eq:is_objective}
\end{equation}

where $\lambda_{\text{IS}} \geq 0$ is the risk-aversion parameter (distinct from Kyle's $\lambda$ in \Cref{subsec:sf_estimators}). The expected implementation shortfall and its variance are:

\begin{equation}
E[\text{IS}] = \eta_{\text{perm}} \sum_k x_k (X - \sum_{j<k} x_j) + \eta \sum_k x_k^2
\end{equation}

and the variance is:

\begin{equation}
\text{Var}[\text{IS}] = \sigma^2 \sum_k \tau \left(X - \sum_{j \leq k} x_j\right)^2
\end{equation}

The analytical solution to \eqref{eq:is_objective} gives the optimal remaining inventory:

\begin{equation}
q_k = X \cdot \frac{\sinh(\kappa (T-k))}{\sinh(\kappa T)}, \qquad \kappa = \sqrt{\frac{\lambda_{\text{IS}} \sigma^2}{\eta}}
\label{eq:is_trajectory}
\end{equation}

where $q_k$ is the inventory remaining after step $k$. The trade size at each step is $x_k = q_{k-1} - q_k$.

At $\lambda_{\text{IS}} = 0$ (risk-neutral): $\kappa \to 0$ and $x_k \to X/T$ (uniform TWAP). As $\lambda_{\text{IS}} \to \infty$ (fully risk-averse): the trajectory front-loads maximally to minimise variance at the expense of market impact.

The urgency ratio $\rho_t = q_{\text{remaining}, t} / t_{\text{remaining}}$ is tracked at each step and used to adjust child order sizes when cumulative execution deviates from the scheduled trajectory (e.g. due to liquidity gaps or partial fills).

\subsection{Stylised Facts Estimators}
\label{subsec:sf_estimators}

\paragraph{Log-returns.} Trade-to-trade log-returns are:
\begin{equation}
r_i = \ln\!\left(\frac{p_i}{p_{i-1}}\right), \quad i = 2, 3, \ldots, n
\end{equation}

\paragraph{Excess kurtosis.} The sample excess kurtosis is the standardised fourth central moment minus 3:
\begin{equation}
\hat{\kappa}_4 = \frac{n^{-1}\sum_i (r_i - \bar{r})^4}{\left(n^{-1} \sum_i (r_i - \bar{r})^2\right)^2} - 3
\end{equation}
Values $\hat{\kappa}_4 > 0$ indicate heavier tails than the Gaussian. The criterion $\hat{\kappa}_4 > 3$ matches the lower bound of the range documented by \citet{cont2001} for tick-level returns. Note that this is not a formal statistical significance test---proper testing requires a test statistic under a null hypothesis and is sensitive to sample size. Fat-tailed return distributions were first documented by \citet{mandelbrot1963}.

\paragraph{Sample autocorrelation.} For a series $\{x_i\}_{i=1}^n$ the sample autocorrelation at lag $k$ is:
\begin{equation}
\hat{\rho}_k = \frac{\sum_{i=k+1}^n (x_i - \bar{x})(x_{i-k} - \bar{x})}{\sum_{i=1}^n (x_i - \bar{x})^2}
\end{equation}
This is computed for three series: raw returns $\{r_i\}$, absolute returns $\{|r_i|\}$, and the trade-sign series $\{D_i\}$ where $D_i = +1$ for buyer-initiated and $-1$ for seller-initiated trades.

\paragraph{Spread decomposition.} Following \citet{huang1997}, let $D_t \in \{+1, -1\}$ denote trade direction and $m_{t+\Delta}$ the mid-price $\Delta = 5$ trades later. The decomposition is:
\begin{align}
\underbrace{2 D_t (p_t - m_t)}_{\text{effective spread}} &= \underbrace{2 D_t (p_t - m_{t+\Delta})}_{\text{realized spread}} + \underbrace{2 D_t (m_{t+\Delta} - m_t)}_{\text{adverse selection}}
\label{eq:spread_decomp}
\end{align}
Realized spread measures the market maker's net revenue conditional on future price discovery. Adverse selection measures the price impact of the trade over the post-trade window.

\paragraph{Kyle's lambda.} The linear price impact coefficient is estimated by OLS (ordinary least squares):
\begin{equation}
\Delta m_t = \lambda \cdot q^{\text{signed}}_t + \varepsilon_t, \quad q^{\text{signed}}_t = D_t \cdot q_t
\label{eq:kyle}
\end{equation}
The power-law exponent $\delta$ in $|\Delta m| = c |q|^\delta$ is estimated by log-log OLS on the absolute values.

\paragraph{Amihud illiquidity.} The \citet{amihud2002} ILLIQ (illiquidity) ratio per trade interval is:
\begin{equation}
\text{ILLIQ}_i = \frac{|r_i|}{q_i}
\end{equation}
The time-series mean $\overline{\text{ILLIQ}}$ measures price impact per unit of volume.

% ─────────────────────────────────────────────────────────────────────────────
\section{Optimisation Methodology}
\label{sec:optimisation}
% ─────────────────────────────────────────────────────────────────────────────

All benchmarks were run on an Intel Core i7-13700F (16 cores: 8 P-cores + 8 E-cores, 24 threads, base clock \SI{2100}{\mega\hertz}) using MSVC 19.44 in Release mode (\texttt{/O2}, NMake generator). CPU caches: L1 data \SI{48}{\kibi\byte} per core, L2 \SI{2048}{\kibi\byte} per core, L3 \SI{30720}{\kibi\byte} shared. All benchmark suites use Google Benchmark v1.9.4 with a minimum of 25 repetitions to obtain stable p50 and p99 estimates.

\subsection{Phase 0: Baseline}

The initial implementation used the three natural C++ standard library containers for a limit order book:

\begin{itemize}[noitemsep]
    \item \textbf{Price index:} \texttt{std::map<Price, Level>} (red-black tree).
    \item \textbf{Per-level queue:} \texttt{std::list<Order>} (doubly-linked heap list).
    \item \textbf{Locator:} \texttt{std::unordered\_map<OrderId, std::list<Order>::iterator>} (separate-chaining hash map with node-level allocation).
\end{itemize}

This is the correct textbook implementation: maps maintain sorted price order, lists support O(1) splice and erase via iterator, and unordered maps give O(1) average-case lookup. However, profiling on both macOS and Windows revealed three sources of cache inefficiency.

First, each \texttt{std::list} node is a separately heap-allocated object. During matching, the engine traverses the list at the best price level, chasing \texttt{node->next} pointers through non-contiguous memory. The hardware prefetcher cannot predict pointer-chasing access patterns; each node access is potentially an L2 or L3 miss.

Second, \texttt{std::map} tree nodes are individually heap-allocated. Even for a small tree (20 price levels), traversal requires following 5--6 random pointers. On the test machine, L2 miss latency is approximately \SI{12}{\nano\second} \citep{fog2024,intel2024}; five misses accumulate to \SI{60}{\nano\second} per lookup, comparable to the target latency for the entire market order processing path.

Third, \texttt{std::unordered\_map} with default settings uses separate chaining. Each \texttt{find} dereferences the bucket array pointer, then the node list pointer, then the iterator pointer stored in the node---three pointer dereferences and two potential cache misses.

\textbf{Baseline measurements:} \texttt{BM\_BookAddRestingLimit} p50 = \SI{888.3}{\nano\second}, \texttt{BM\_BookCancel\_O1} p50 = \SI{899.9}{\nano\second}, \texttt{BM\_BookModifyQty\_O1} p50 = \SI{1677.5}{\nano\second}.

\subsection{Phase 1: Vector Queue with Tombstone Cancellation}

\paragraph{Motivation.} The dominant cost in \texttt{add\_resting\_limit} was the \texttt{operator new} call for a new list node: approximately \SI{60}{\nano\second} on Windows due to the allocator's free-list management overhead. The dominant cost in \texttt{cancel} was the corresponding \texttt{delete}, which returns memory to the allocator.

\paragraph{Change.} Each \texttt{Level} replaces \texttt{std::list<Order>} with \texttt{std::vector<Order>}. Insertion is \texttt{push\_back}: amortised $O(1)$, no per-element allocation. Cancellation sets \texttt{Order::qty = 0} (tombstone) at the absolute index \texttt{abs\_idx} stored in the locator, rather than erasing from the list.

During matching, the engine skips tombstones before each match step. Since real cancellations are sparse (empirically $<$ 5\% of resting orders are cancelled before being matched), the expected number of tombstones to skip is near zero. When a maker order is fully consumed, \texttt{front\_offset} advances by one rather than calling \texttt{pop\_front}.

The locator now stores \texttt{abs\_idx: uint32\_t} rather than a list iterator. Unlike 	exttt{std::list} — whose pointer-based nodes are cache-unfriendly even though its iterators are stable across modifications — a 	exttt{std::vector} keeps all order nodes contiguous in memory, enabling sequential hardware prefetching during matching sweeps.

\paragraph{Effect.}
\begin{center}
\begin{tabular}{lrr}
\toprule
Benchmark & Before (p50) & After (p50) \\
\midrule
BM\_BookAddRestingLimit & \SI{888.3}{\nano\second} & \SI{161}{\nano\second} \\
BM\_BookCancel\_O1 & \SI{899.9}{\nano\second} & \SI{136}{\nano\second} \\
\bottomrule
\end{tabular}
\end{center}

The 5.5$\times$ and 6.6$\times$ improvements are driven almost entirely by eliminating heap allocation from the hot path.

\subsection{Phase 2: FlatPriceMap}

\paragraph{Motivation.} \texttt{std::map} tree traversal involves $O(\log N_p)$ pointer dereferences through non-contiguous memory. For $N_p = 20$ levels, $\lceil\log_2 20\rceil = 5$ dereferences each risk an L2 cache miss.

\paragraph{Change.} \texttt{FlatPriceMap<Level, Cmp>} stores entries as a sorted \texttt{std::vector<Entry>}. The \texttt{find(px)} and \texttt{operator[](px)} operations use \texttt{std::lower\_bound}---binary search over a contiguous array. At 20 levels this is 5 comparisons over a contiguous region fitting comfortably in a single L1 cache line (512 bytes at 20 $\times$ 24 bytes per entry header).

\paragraph{Effect.} FlatPriceMap is an enabling change: it establishes the contiguous memory layout required for Phases 3 and 4 and eliminates red-black tree pointer chasing on the market order path. This improvement does not appear in Table~\ref{tab:optresults} because the benchmark operations used for that table (book add, cancel, sweep, depth) are dominated by allocation and hash-map costs that Phase 2 alone does not address. The full benefit of FlatPriceMap is only visible in combination with Phases 3 and 4.

\subsection{Phase 3: O(1) Front Erase via front\_offset}

\paragraph{Motivation.} \texttt{BM\_ProcessMarket\_SweepKLevels} measured \SI{103}{\nano\second} per level on Windows versus \SI{27.6}{\nano\second} on macOS---a 3.7$\times$ platform-specific regression. Root-cause analysis identified \texttt{FlatPriceMap::erase(begin())}: erasing the first element of a \texttt{std::vector} calls \texttt{std::memmove} to shift all subsequent entries left. On Windows, the MSVC implementation of this shift has higher overhead than on macOS/clang for the particular entry size ($\sim$40 bytes per \texttt{Level} header). For a $K$-level sweep, $K$ such shifts produce $\sum_{i=1}^{K}(N_p - i) = O(K^2)$ total element movements.

\paragraph{Change.} \texttt{FlatPriceMap} gains a \texttt{front\_offset\_} field. \texttt{erase(begin())} becomes \texttt{++front\_offset\_}---$O(1)$ unconditionally. All iterator arithmetic and \texttt{lower\_bound} calls use \texttt{begin() + front\_offset\_} as the effective start. Lazy compaction fires when \texttt{front\_offset\_ > size() / 2} and \texttt{front\_offset\_ > 8}, shifting the live entries to the beginning in $O(N_p)$ and resetting the offset to 0.

For a $K$-level sweep, compaction fires $O(\log K)$ times, each costing $O(N_p)$ work. Total work is approximately linear in $K$ in practice (the $O(\log K)$ compactions each touch $O(K/2)$ entries but this is dominated by the $K$ single-step O(1) erases); this resolves the quadratic $O(K^2)$ baseline.

\paragraph{Effect.}
\begin{center}
\begin{tabular}{lrr}
\toprule
Benchmark & Before & After \\
\midrule
BM\_ProcessMarket\_SweepKLevels (p50) & \SI{103}{\nano\second} & \SI{26.3}{\nano\second} \\
BM\_ProcessMarket\_SweepKLevels (p99) & \SI{115.3}{\nano\second} & \SI{26.5}{\nano\second} \\
\bottomrule
\end{tabular}
\end{center}

The Windows platform regression was completely eliminated; Windows performance now matches the macOS result.

\subsection{Phase 4: O(1) Depth via live\_count}

\paragraph{Motivation.} \texttt{BM\_BookDepth\_TopN} measured \SI{2107.5}{\nano\second} (p50) on Windows, a 23$\times$ regression relative to macOS (\SI{90.2}{\nano\second}). The root cause was the order-count computation in \texttt{depth()}: to determine the number of live (non-tombstone) orders at each price level, the method iterated \texttt{Level::q} from \texttt{front\_offset} to \texttt{q.size()}, checking \texttt{Order::qty > 0} for each entry. After a long simulation where many orders had been cancelled at a popular price level, \texttt{q.size()} could be orders of magnitude larger than the number of live orders, making this an $O(|q|)$ scan rather than $O(1)$.

\paragraph{Change.} Each \texttt{Level} gains a \texttt{live\_count: uint32\_t} field, maintained incrementally:
\begin{itemize}[noitemsep]
    \item \texttt{add\_resting\_limit}: \texttt{++live\_count}
    \item \texttt{cancel}: \texttt{--live\_count}
    \item Full consumption during matching: \texttt{--live\_count}
\end{itemize}

The \texttt{depth()} method reads \texttt{lvl.live\_count} directly, eliminating the linear scan.

\paragraph{Effect.}
\begin{center}
\begin{tabular}{lrr}
\toprule
Benchmark & Before & After \\
\midrule
BM\_BookDepth\_TopN (p50) & \SI{2107.5}{\nano\second} & \SI{84.0}{\nano\second} \\
BM\_BookDepth\_TopN (p99) & \SI{2494.7}{\nano\second} & \SI{88.6}{\nano\second} \\
\bottomrule
\end{tabular}
\end{center}

This was the largest single improvement of the campaign: 25.1$\times$ reduction at p50. The improvement is purely algorithmic (O(N) to O(1)), independent of hardware.

\subsection{Phase 5: SmallVector for MatchResult}

\paragraph{Motivation.} \texttt{MatchResult::trades} was a \texttt{std::vector<Trade>}. Every call to \texttt{engine.process()} that generates trades triggered a heap allocation for the vector's storage buffer. On Windows, the MSVC CRT (Microsoft Visual C++ C Runtime) allocator has approximately \SI{50}{\nano\second} overhead per allocation under contention-free conditions.

\paragraph{Change.} \texttt{SmallVector<Trade, 4>} provides a 4-element inline buffer stored directly in the \texttt{MatchResult} struct, which lives on the call stack frame of \texttt{process()}. For 0--4 trades, no heap interaction occurs. The implementation falls back to heap allocation for $>$4 trades by moving the existing inline buffer to a new heap allocation.

\paragraph{Effect.} The hot path (single market order filling 1--3 resting orders) becomes entirely allocation-free. This does not appear as a change in the p50 column of Table~\ref{tab:optresults} because the median measurement was already allocation-free in most repetitions; the benefit is visible in variance reduction and p99 tightening. \texttt{BM\_ProcessMarketOrder} p99 is \SI{47.1}{\nano\second} on the final calibrated build.

\subsection{Phase 6: next\_event\_ts\_ Cache}

\paragraph{Motivation.} The \texttt{flush(Ts ts)} call at the start of each simulation tick checks whether any phase-transition events---auction end, halt expiry, volatility-interruption timeout---are due by comparing \texttt{ts} against cached phase-end timestamps (\texttt{auction\_end\_ts\_}, \texttt{halt\_end\_ts\_}, \texttt{tal\_end\_ts\_}). In a typical continuous-trading simulation, no such events are scheduled and the check always finds nothing. The baseline implementation performed the full timestamp comparison and branch on every call regardless.

\paragraph{Change.} \texttt{MatchingEngine} caches the timestamp of the next scheduled event as \texttt{next\_event\_ts\_}. The \texttt{flush(ts)} method returns immediately---without locking, without queue inspection---when \texttt{ts < next\_event\_ts\_}. The cache is updated only when an event is scheduled or processed.

\paragraph{Effect.} At 1000 steps/second, approximately 2000 redundant queue checks per second of simulated time are eliminated. The primary benefit is reduced cache pollution: the event queue data structure no longer occupies cache lines that would otherwise hold hot book data. This phase does not produce a measurable change in the isolated benchmark operations reported in Table~\ref{tab:optresults}; its effect is visible only in end-to-end simulation throughput.

\subsection{Phase 7: Robin Hood FlatHashMap}

\paragraph{Motivation.} \texttt{std::unordered\_map<OrderId, Locator>} with separate chaining stores each entry as a heap-allocated node. \texttt{find(id)} dereferences (1) the bucket array, (2) the chain head pointer, (3) each node's \texttt{key} and \texttt{next} pointer. This produces 2--3 cache misses per lookup. For \texttt{BM\_BookCancel\_O1} and \texttt{BM\_BookModifyQty\_O1}, which each require one locator lookup on the critical path, these misses account for 60--120 ns of the total measurement.

\paragraph{Change.} \texttt{FlatHashMap<K, V>} uses Robin Hood hashing with linear probing, as described in \Cref{sec:datastructures}. The entire 256-slot map (6 KB) fits in L1 cache, eliminating pointer chasing. The Robin Hood probe-distance invariant enables early termination in \texttt{find} and fast backward-shift in \texttt{erase}.

Three API innovations address the specific usage patterns of \texttt{OrderBook::loc\_}:
\begin{enumerate}[noitemsep]
    \item \texttt{extract(k, out)}: find + erase in one probe pass. Used by \texttt{cancel}.
    \item \texttt{insert\_new(k, v)}: insert without equality check (safe for fresh OrderIds). Used by \texttt{add\_resting\_limit}.
    \item \texttt{find(k) $\to$ V*}: returns a raw pointer; no iterator allocation. Used by \texttt{modify\_qty} and \texttt{queue\_info}.
\end{enumerate}

\paragraph{Effect.}
\begin{center}
\begin{tabular}{lrr}
\toprule
Benchmark & Before (Phase 0 baseline) & After (Phase 7) \\
\midrule
BM\_BookCancel\_O1 (p50) & \SI{899.9}{\nano\second} & \SI{127.7}{\nano\second} \\
BM\_BookAddRestingLimit (p50) & \SI{888.3}{\nano\second} & \SI{150.2}{\nano\second} \\
BM\_BookModifyQty\_O1 (p50) & \SI{1677.5}{\nano\second} & \SI{221.8}{\nano\second} \\
\bottomrule
\end{tabular}
\end{center}

\texttt{BM\_BookModifyQty\_O1} remains at \SI{222}{\nano\second} despite the hash map improvement because \texttt{modify\_qty} additionally accesses the \texttt{FlatPriceMap} (binary search) and the \texttt{Level::q} vector (random heap access). These two additional memory regions are likely not in L1 cache when \texttt{modify\_qty} is called, producing one L2 miss (${\approx}\SI{12}{\nano\second}$) and one L3 miss (${\approx}\SI{47}{\nano\second}$) \citep{fog2024,intel2024} that together account for approximately \SI{59}{\nano\second} of the remaining measurement, consistent with the \SI{222}{\nano\second} observed.

\subsection{Phase 8: Run-Loop Micro-Optimisations}

Three targeted changes in \texttt{world.cpp} address redundant computation in the main simulation loop:

\paragraph{best\_bid/best\_ask reuse.} Step B of the run loop (\Cref{alg:runloop}) computes \texttt{best\_bid()}, \texttt{best\_ask()}, and \texttt{mid}. Previously, steps F (BookTop snapshot) and G (PnL snapshot) each called these accessors again, producing two additional \texttt{FlatPriceMap::begin()} reads per tick. At \SI{1000}{steps/second} this amounts to approximately 4000 eliminated function calls per second of simulation time.

\paragraph{Insertion sort for latency buffer.} When latency is enabled, agent actions are collected in a buffer and sorted by effective timestamp before dispatch. The baseline used \texttt{std::stable\_sort} ($O(N \log N)$, allocates a temporary merge buffer of $O(N/2)$ elements). With 4--8 agents submitting 2--4 actions each, the buffer contains 8--32 elements per tick---well within the regime where insertion sort ($O(N^2)$ worst case, $O(N)$ best case, zero allocations) dominates. In the typical case where all actions share the same timestamp, insertion sort terminates in $O(N)$ without any swaps.

\paragraph{StylisedFactsMeasurer::reserve.} The internal \texttt{tops\_} and \texttt{trades\_} accumulation vectors now accept a capacity hint at construction: \texttt{sfm.reserve(n\_steps, est\_fills)}. Without this, both vectors grow by doubling from capacity 0, triggering $\log_2(n\_steps) \approx 15$ reallocations during a 30,000-step run.

\subsection{Optimisation Summary}

\Cref{tab:optresults} summarises the complete optimisation journey.

\begin{table}[H]
\centering
\caption{Optimisation results: p50 latency in nanoseconds across all phases. All measurements on Windows, MSVC 19.44, Release.}
\label{tab:optresults}
\resizebox{\textwidth}{!}{%
\begin{tabular}{lrrrr}
\toprule
Phase & Add (ns) & Cancel (ns) & Sweep/lvl (ns) & Depth (ns) \\
\midrule
0 Baseline & 888 & 900 & 103 & 2107 \\
1 Vector queue + tombstone & 161 & 136 & 103 & 2107 \\
2 FlatPriceMap & 161 & 136 & 103 & 2107 \\
3 front\_offset O(1) erase & 161 & 136 & \textbf{28} & 2107 \\
4 live\_count O(1) depth & 161 & 136 & 28 & \textbf{85} \\
5 SmallVector<Trade,4> & 161 & 136 & 28 & 85 \\
6 next\_event\_ts\_ cache & 161 & 136 & 28 & 85 \\
7 Robin Hood FlatHashMap & 150 & 128 & 26 & 84 \\
8 Run-loop micro-opts & 150 & 128 & 26 & 84 \\
\midrule
\textbf{Total speedup} & \textbf{5.9}$\times$ & \textbf{7.0}$\times$ & \textbf{4.0}$\times$ & \textbf{25.1}$\times$ \\
\bottomrule
\end{tabular}%
}% end resizebox
\end{table}

% ─────────────────────────────────────────────────────────────────────────────
\section{Demonstration: Stylised Fact Emergence as Platform Expressiveness}
\label{sec:validation}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Simulation Setup}

The validation simulation runs for $T = 300$ seconds at $\Delta t = \SI{1}{\milli\second}$ resolution (300,001 steps). The agent population consists of:

\begin{itemize}[noitemsep]
    \item \textbf{5 Hawkes noise traders} (owner IDs 1--5): each with $p_{\text{mkt}} = 0.5$, lot size 2, independent Hawkes processes seeded from the global seed via splitmix64 \citep{steele2014,steele2021}.
    \item \textbf{1 Avellaneda-Stoikov market maker} (owner ID 10): default A-S parameters with EWMA volatility estimation and imbalance skew.
    \item \textbf{2 fundamental value agents} (owner IDs 20, 21): FundValue-A has $\theta = 1$, $\sigma_V = 0.3$, lot size 2 (aggressive); FundValue-B has $\theta = 3$, $\sigma_V = 0.2$, lot size 1 (conservative). Both use the fixed-anchor OU process with $\kappa = 0.005$.
    \item \textbf{1 momentum agent} (owner ID 30): entry band $\delta_{\text{entry}} = 2$ ticks, default EMA parameters.
\end{itemize}

The Implementation Shortfall (Almgren-Chriss) agent is documented in \Cref{sec:math} for completeness but is not included in the validation configuration, which focuses on market-making, noise trading, informed trading, and trend-following as the four canonical agent archetypes.

The book is initialised with 20 symmetric price levels at mid-price 10,000 ticks with 10 lots per level. All five stylised fact criteria use the thresholds from \Cref{subsec:sf_estimators}.

\paragraph{Calibration procedure and validation scope.} Agent parameters were selected iteratively to produce stable, non-runaway price dynamics across the 50-seed robustness study---not calibrated to empirical moment targets from real market data. The validation therefore demonstrates that this agent ecology \emph{can} produce stylised-fact-consistent output; it does not establish that the simulation \emph{replicates} any specific real market. Calibration to empirical LOBster data is reserved for future work.

\paragraph{Sample size notation.} Throughout this section, $n$ denotes the number of trade-to-trade log-returns, equal to the number of trades minus one. The single-seed reference run (seed=42) generates 774 trades and therefore $n=773$ returns. All stylised fact statistics are computed on this return series unless stated otherwise.

\subsection{Return Distribution}

\Cref{tab:returns} reports the estimated return distribution statistics.

\begin{table}[H]
\centering
\caption{Trade-to-trade log-return distribution ($n=773$, seed=42, calibrated parameters).}
\label{tab:returns}
\begin{tabular}{lrl}
\toprule
Statistic & Value & Interpretation \\
\midrule
Mean & ${\sim}0$ & Near-zero, consistent with martingale prices \\
Excess kurtosis & $\mathbf{4.04}$ & Bootstrap 95\,\% CI $[2.51, 5.56]$; literature range 3--10 \\
Std dev & ${\sim}9 \times 10^{-4}$ & ${\sim}$9 bps per trade; realistic intraday volatility \\
\bottomrule
\end{tabular}
\end{table}

The excess kurtosis of 4.04 (bootstrap 95\,\% CI $[2.51, 5.56]$) falls within the 3--10 range documented by \citet{cont2001} for intraday equity returns, confirming fat-tailed dynamics at the calibrated parameter setting.

\subsection{Autocorrelation Structure}

\Cref{tab:autocorr} reports the lag-1 autocorrelation coefficients for three series.

\begin{table}[H]
\centering
\caption{Sample lag-1 autocorrelations with 95\,\% CIs ($n=773$, seed=42).
  CIs are asymptotic ($\pm 1.96/\sqrt{n}$). Literature ranges from
  \citet{cont2001} and \citet{bouchaud2004}.}
\label{tab:autocorr}
\resizebox{\textwidth}{!}{%
\begin{tabular}{lrrll}
\toprule
Series & Value & 95\,\% CI & Reference & Status \\
\midrule
Return AC & $-0.451$ & $[-0.521,\,-0.380]$ & Roll (1984) & \checkmark \\
$|\text{Return}|$ AC & $0.323$ & $[0.252,\;0.393]$ & Engle (1982) & \checkmark \\
Trade-sign AC & $0.270$ & $[0.200,\;0.341]$ & Bouchaud et al. (2004) & $\sim^\ddagger$ \\
\bottomrule
\end{tabular}%
}% end resizebox
\end{table}

\paragraph{Bid-ask bounce.} The negative return AC ($-0.451$, 95\,\% CI $[-0.521, -0.380]$) is the bid-ask bounce documented by \citet{roll1984}: a buy market order hits the ask, raising the transaction price above the mid; the subsequent reversal to the bid produces a negative first-order autocorrelation in price changes. \citet{roll1984} shows this autocorrelation is negative and proportional to the squared effective spread; our observed value of $-0.451$ is qualitatively consistent with the spread level of 8.86 ticks documented in \Cref{tab:spreads}. The negative return AC ($-0.451$) and positive trade-sign AC ($+0.270$) are not contradictory: positive flow AC reflects consecutive same-direction orders, while negative return AC reflects bid-ask bounce at the transaction price level when those orders alternate between hitting the bid and ask quotes.

\paragraph{Volatility clustering.} The absolute-return AC ($0.323$, 95\,\% CI $[0.25, 0.39]$) confirms that large price moves tend to cluster together in time. This is the defining empirical property of the ARCH/GARCH family of models \citep{engle1982, bollerslev1986} and is documented across virtually all financial assets at every time scale \citep{cont2001}. Our value of 0.323 sits within the 0.10--0.40 range for intraday data, indicating that the Hawkes self-excitation mechanism generates realistic volatility persistence without requiring any explicit GARCH specification.

\paragraph{Order flow autocorrelation.} The trade-sign AC of $0.270$ (95\,\% CI $[0.200, 0.341]$, 50-seed median $0.327$) sits at the lower edge of the $[0.30, 0.70]$ literature range at seed=42. The 50-seed robustness study confirms the median across seeds is 0.327 (see \Cref{tab:multiseed}), firmly within the documented range. The regularity arises through Hawkes self-excitation: noise traders submit same-direction bursts when arrival intensity is elevated, creating persistent order-flow autocorrelation without any explicit order-splitting model.

\subsection{Spread Statistics and Decomposition}

\Cref{tab:spreads} reports the spread statistics.

\begin{table}[H]
\centering
\caption{Bid-ask spread decomposition ($\Delta = 5$ trades, $n=773$). Decomposition identity: effective $=$ realized $+$ adverse selection.}
\label{tab:spreads}
\begin{tabular}{lrl}
\toprule
Statistic & Value (ticks) & Note \\
\midrule
Time-weighted spread & $8.86$ & Single market maker; wide relative to liquid equities \\
Effective spread & $9.89$ & Cost paid by market-order submitters \\
Realized spread & $+36.38$ & Market maker net revenue; positive \\
Adverse selection & $-26.51$ & Informed trader component \\
\midrule
Check: real + adv sel & $9.87$ & $\approx 9.89$ \checkmark \\
\bottomrule
\end{tabular}
\end{table}

The spread decomposition shows a positive realized spread ($+36.38$ ticks) and negative adverse selection ($-26.51$ ticks), indicating that the market maker earns net revenue on its fills and that post-trade price movement is favourable to the maker on this stochastic path. The effective spread of 9.89 ticks reflects the average cost paid by takers. \begin{remark}[Spread context]
The time-weighted spread of 8.86 ticks is wide relative to liquid equities (typically 1--3 ticks on NASDAQ). This reflects the simulation's single market maker facing unsophisticated counterparties with no competing liquidity providers. Calibration to a more competitive market-making regime would produce narrower spreads.
\end{remark}

The decomposition identity holds to within rounding ($36.38 + (-26.51) = 9.87 \approx 9.89$), validating the Huang-Stoll estimator implementation. This is consistent with the Grossman-Stiglitz (1980) equilibrium \citep{grossman1980}: market makers earn positive rents on noise-trader flow even when individual informed fills are unfavourable. The market maker earns $+10{,}047$ ticks over the 300-second horizon, consistent with the A-S framework: its inventory-adjusted quoting captures spread income from noise trader flow while limiting adverse-selection exposure through continuous quote updates. The negative adverse selection component ($-26.51$ ticks) in the \texttt{StylisedFactsMeasurer} output reflects the trade-to-trade mid-price estimator used internally. The standard Huang-Stoll interpretation uses the order-arrival mid-price: a fill-level decomposition gives positive adverse selection ($+1.38$ ticks at $\Delta=5$), the economically expected sign. The prominent $-26.51$ ticks figure in Table~\ref{tab:spreads} uses the post-trade mid-price convention, which captures the A-S market maker's mean-reversion and reverses the sign. Both values are reported; the fill-level figure ($+1.38$ ticks) is the standard Huang-Stoll result. The A-S market maker's continuous re-pricing after each fill attenuates post-trade drift, which is the mechanism in both cases.

\paragraph{Spread decomposition sensitivity.} \Cref{tab:spread_sensitivity} reports the fill-level decomposition across four post-trade windows. The effective spread varies by less than 6\% across $\Delta \in \{3,5,10,20\}$, confirming that the choice of $\Delta=5$ is not cherry-picked.

\begin{table}[H]
\centering
\caption{Spread decomposition sensitivity to post-trade window $\Delta$ (fill-level estimator, seed=42). All values in ticks. Identity: Eff $=$ Real $+$ Adv holds exactly at all $\Delta$.}
\label{tab:spread_sensitivity}
\begin{tabular}{rrrrrr}
\toprule
$\Delta$ & $n$ & Eff spread & Real spread & Adv select & Check \\
\midrule
3  & 1267 & $+4.89$ & $+3.25$ & $+1.64$ & $4.89$ \\
5  & 1265 & $+4.87$ & $+3.49$ & $+1.38$ & $4.87$ \\
10 & 1260 & $+4.80$ & $+3.89$ & $+0.92$ & $4.80$ \\
20 & 1250 & $+4.64$ & $+4.04$ & $+0.60$ & $4.64$ \\
\bottomrule
\end{tabular}
\end{table}

\subsection{Price Impact}

The Kyle lambda estimate from \eqref{eq:kyle} is $\hat{\lambda} = -5.79$ ticks/lot with $R^2 = 0.00054$. The near-zero $R^2$ indicates that the linear regression has no explanatory power at this sample size; the sign of $\hat{\lambda}$ is therefore unreliable. A reliable estimate would require substantially larger samples---in practice $n \gg 500$---and a more carefully calibrated agent population. The power-law exponent is $\hat{\delta} = -0.39$ (theoretical prediction $\approx 0.5$), similarly unreliable at this sample size and should not be interpreted.

\begin{remark}
One plausible explanation for the low $R^2$ is that the A-S market maker continuously re-prices quotes after each trade, attenuating the sustained post-trade price drift that Kyle's $\lambda$ estimator captures. A second explanation is the limited sample size ($n=773$): OLS has negligible power to detect a small but real price impact signal in so few observations. A simulation designed to isolate price impact would pair a single informed trader with a passive market maker and collect substantially more trades before estimation.
\end{remark}

\subsection{Agent Economics}

\Cref{tab:agentpnl} summarises the per-agent activity and PnL.

\begin{table}[H]
\centering
\caption{Per-agent TCA summary (seed=42, horizon=300\,s). PnL in raw ticks ($\text{mtm}_t^{(i)} = X_t^{(i)} + q_t^{(i)} \cdot m_t$, mark-to-market at final step). Total PnL across all agents sums to zero, confirming closed zero-sum accounting. Individual agent outcomes vary across seeds; these figures illustrate a representative run.}
\label{tab:agentpnl}
\begin{tabular}{lrrrr}
\toprule
Agent & Orders & Fills (M/T) & Avg slip (tk) & Final PnL (tk) \\
\midrule
HawkesNoise-1 & 112 & 74/75 & $+$5.57 & $+$80,040 \\
HawkesNoise-2 & 118 & 70/86 & $+$5.70 & $-$150,066 \\
HawkesNoise-3 & 140 & 96/79 & $+$5.56 & $+$230,253 \\
HawkesNoise-4 & 94 & 73/53 & $+$7.06 & $-$49,941 \\
HawkesNoise-5 & 127 & 109/71 & $+$5.45 & $+$80,278 \\
MarketMakerAS & 3120 & 74/109 & $+$5.46 & $+$10,047 \\
FundValue-aggressive & 107 & 0/139 & $+$5.35 & $-$660,361 \\
FundValue-slow & 61 & 0/61 & $+$5.93 & $+$500,004 \\
Momentum & 94 & 0/101 & $+$7.63 & $-$40,254 \\
\bottomrule
\end{tabular}
\end{table}

The market maker submits 3120 orders but achieves only 74 maker fills and 109 taker fills, consistent with its role as a continuous two-sided quoter whose resting orders are frequently cancelled and resubmitted as the reservation price updates. Its average slippage of $+5.46$ ticks reflects the spread cost of taker fills, while maker fills contribute negative slippage that partially offsets this.

Under this stochastic path (seed=42), FundValue-slow earns the largest positive PnL ($+500{,}004$ ticks) while FundValue-aggressive incurs the largest loss ($-660{,}361$ ticks). This reversal relative to naive expectation illustrates the path-dependence of agent outcomes in a chaotic multi-agent system: the `aggressive' label refers to threshold sensitivity ($\theta = 1$ tick), not to profitability. With a tight threshold, FundValue-aggressive trades frequently (107 orders, 139 taker fills) and accumulates a large long position ($+$66 lots) that is adversely marked at simulation end. FundValue-slow, with its wider threshold, builds a smaller but oppositely-signed position ($-$50 lots) that happens to be favourably marked. The market maker's PnL ($+10{,}047$ ticks) is modest and positive, consistent with the A-S optimal quoting framework's inventory management discipline. Readers should not over-interpret single-seed agent outcomes; the 50-seed robustness study in \Cref{subsec:multiseed} characterises the distribution of outcomes across seeds.

Wealth conservation is verified exactly: total mark-to-market PnL, total cash PnL, and net position all sum to zero across all nine agents (confirmed by \texttt{src/python/pnl\_conservation\_check.py}), confirming the simulation operates as a closed zero-sum system. Every tick gained by one agent was lost by another. This can be independently verified by running \texttt{src/python/pnl\_conservation\_check.py}.


\subsection{Multi-Seed Robustness Study}
\label{subsec:multiseed}

To confirm that the stylised facts results are not specific to seed=42,
we ran 50 independent seeds (1000--1049) via subprocess-isolated processes,
each simulating 300 seconds with the same calibrated 9-agent configuration.
A pybind11 double-ownership defect (corrected in the course of this work)
had previously caused heap corruption on the second world creation; following
that fix, all 50 seeds completed without error.

\Cref{tab:multiseed} reports pass rates and cross-seed summary statistics.

\begin{table}[H]
\centering
\caption{Multi-seed robustness study: 50 seeds $\times$ 300\,s, calibrated 9-agent configuration. Denominators reflect available data: two seeds are excluded from kurtosis/AC rows (denominator 48) for distinct reasons, confirmed by replaying each seed. Seed~1029 generated 561 trades with a 787-tick price drift; the resulting outlier return distribution produced kurtosis exceeding the numerically reliable range ($\hat{\kappa}_4 > 500$), which we exclude as a degenerate draw rather than a structural failure. Seed~1048 experienced a complete price explosion: the mid-price collapsed from 10{,}000 to $-4{,}877{,}547$ ticks, the market maker accumulated a position of $-4{,}694$ lots across 115{,}737 orders, and return statistics are numerically meaningless on this path. Seed~1048 is also excluded from spread statistics (denominator 49). Flow AC uses all 50 seeds.}
\label{tab:multiseed}
\resizebox{\textwidth}{!}{%
\begin{tabular}{lrrrrl}
\toprule
Fact & Pass rate & Median & Mean & Std & Criterion \\
\midrule
Fat tails ($\hat{\kappa}_4 > 3$)
  & \textbf{46/48}~(96\,\%) & 7.04 & 102.8$^\star$ & 170.5 & $> 3$ \\
Vol.\ clustering ($\hat{\rho}(|\text{ret}|) > 0.05$)
  & \textbf{46/48}~(96\,\%) & 0.212 & 0.210 & 0.095 & $> 0.05$ \\
Flow AC ($|\hat{\rho}(\text{sign})| > 0.10$)
  & \textbf{50/50}~(100\,\%) & 0.327 & 0.329 & 0.084 & $> 0.10$ \\
Positive spread ($\bar{s} > 0$)
  & \textbf{49/50}~(98\,\%) & 8.37\,ticks & --- & --- & $> 0$ \\
Return AC negative
  & \textbf{47/48}~(98\,\%) & $-0.317$ & $-0.281$ & 0.121 & $< 0$ \\
\bottomrule
\end{tabular}%
}% end resizebox
\smallskip\\
{\footnotesize $^\star$ Mean kurtosis inflated by 15 seeds with occasional
  extreme returns (genuine fat-tail events from informed-flow sweeps, not
  model failures). At the transaction level, \citet{cont2001} documents
  kurtosis exceeding 20 in real LOB data; our median of 7.04 falls within
  the empirically documented range.}
\end{table}

All four pass rates meet or exceed 92\,\% across 50 independent seeds,
with pass rates of 92--100\,\% across all seeds (denominator 50, including degenerate seeds) and 96--100\,\% among eligible seeds (denominator 48, excluding two seeds with structural failures documented in the table caption). The 90\,\% threshold was set before examining per-seed results but was not formally pre-registered.
The return autocorrelation is negative in 47 of 48 seeds with sufficient
data (median $-0.317$), confirming the bid-ask bounce is a structural
property of the calibrated agent ecology. The multi-seed study was run on
the v2.1.1 codebase following correction of a pybind11 double-ownership
defect that was identified and fixed before any multi-seed results were generated; reported pass rates reflect the corrected implementation. Kurtosis exceeds 3 in 46 of 48
seeds; the median of 7.04 falls within the $[3,10]$ range documented by
\citet{cont2001}. The kurtosis threshold of $>3$ is intentionally conservative; the median of 7.04 across 50 seeds is the more informative statistic.
Seeds with kurtosis ${>}10$ exhibit genuine fat-tail
behaviour from occasional large informed-flow sweeps, consistent with
real transaction-level data.

\subsection{Stylised Facts Validation Summary}

\begin{remark}[Sample size and statistical reliability]
The simultaneous emergence of these stylised facts within a 300-second
horizon ($n=773$, seed=42) illustrates rapid equilibration of
the agent ecology. The 50-seed robustness study (\Cref{subsec:multiseed})
confirms pass rates of 96--100\,\% across independent seeds at $n=773$. Readers
should treat single-seed values as illustrative; the median statistics across
seeds (\Cref{tab:multiseed}) are the appropriate summary for the calibrated
agent configuration.
\end{remark}

\Cref{tab:sfvalidation} summarises the validation outcome.

\begin{table}[H]
\centering
\caption{Stylised facts validation summary ($n=773$, seed=42, calibrated parameters).
  Kurtosis 95\,\% CI from 2000-resample bootstrap; AC 95\,\% CIs asymptotic ($\pm 1.96/\sqrt{n}$).
  Criteria from \citet{cont2001} and \citet{bouchaud2004}.}
\label{tab:sfvalidation}
\resizebox{\textwidth}{!}{%
\begin{tabular}{llrrll}
\toprule
Fact & Criterion & Value & 95\,\% CI & Literature & Status \\
\midrule
Fat tails & $\hat{\kappa}_4 > 3$ & 4.04 & $[2.51,\;5.56]$ & 3--10 (Cont 2001) & \checkmark \\
Volatility clustering & $\hat{\rho}_1(|\text{ret}|) > 0.05$ & 0.323 & $[0.25,\;0.39]$ & 0.10--0.40 & \checkmark \\
Order flow AC & $|\hat{\rho}_1(D)| > 0.10$ & 0.270 & $[0.20,\;0.34]$ & 0.30--0.70 & $\sim^\ddagger$ \\
Positive spread & $\bar{s} > 0$ & 8.86\,ticks & --- & positive & \checkmark \\
Price impact$^\dagger$ & inconclusive & $R^2{=}0.0005$ & n/a & $\times$ \\
\midrule
\multicolumn{6}{l}{\textbf{4 of 5 facts robust at $\geq$96\,\% across 50 seeds;\enspace pass rates 96--100\,\%;\enspace see \Cref{tab:multiseed}}} \\
\bottomrule
\end{tabular}%
}% end resizebox
\smallskip\\
{\footnotesize $^\dagger$\,Price impact: $|\hat{\lambda}|>0$ mechanically but $R^2{\approx}0.0005$ is statistically meaningless; the A-S market maker re-prices quotes after each trade, attenuating the post-trade drift that Kyle's $\lambda$ captures.\\
$^\ddagger$\,Trade-sign AC at seed=42 is 0.270 (lower edge of literature range). The 50-seed median is 0.327, within $[0.30, 0.70]$; see \Cref{tab:multiseed}.}
\end{table}

\subsection{Emergent vs.\ Idiosyncratic: A Structural Observation}

A comparison between the v2.1.1 codebase and an earlier version of the same seed=42 run (differing in matching order and \texttt{Trade} struct layout) reveals a structurally important result. The aggregate market statistics---excess kurtosis ($4.04$), volatility clustering AC ($0.323$), trade-sign AC ($0.270$), and time-weighted spread ($8.86$ ticks)---are identical to three significant figures across code versions that differ in matching order and Trade struct layout, and therefore produce entirely different stochastic paths. In contrast, individual agent PnL values change by hundreds of thousands of ticks with sign reversals (e.g.\ FundValue-aggressive: $+119{,}993$ in one version, $-660{,}361$ in another).

This dichotomy---aggregate stability with idiosyncratic variance---is a hallmark of real financial markets: market-level statistical regularities coexist with enormous variance in individual trader outcomes. It confirms that the stylised facts in \Cref{tab:sfvalidation} are emergent properties of the agent \emph{ecology's structure} (the interaction rules between Hawkes noise traders, an A-S market maker, fundamental value agents, and a momentum trader) rather than artefacts of a particular stochastic realisation. The market maker's PnL ($+10{,}047$ ticks) is positive in this representative run, consistent with the A-S framework's inventory management discipline being a structural property of the quoting algorithm rather than luck.

% ─────────────────────────────────────────────────────────────────────────────
\section{Performance Evaluation}
\label{sec:perf}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Benchmark Methodology}

All benchmarks use Google Benchmark v1.9.4 with the following configuration:
\begin{itemize}[noitemsep]
    \item \textbf{Hardware:} Intel Core i7-13700F, 16 cores (8P\,+\,8E), 24 threads, base clock \SI{2100}{\mega\hertz}. Primary benchmarks were run without core pinning; OS scheduling across the hybrid P/E-core topology introduces variability between repetitions, which is why we report p50 rather than minimum. Turbo boost was available and active, so the reported figures reflect realistic sustained clock speeds rather than base-clock performance. A supplementary run with processor affinity pinned to P-core~0 (\texttt{ProcessorAffinity\,=\,1}) produced latencies 10--35\,\% higher across all benchmarks. The direction is counterintuitive since P-core 0 should still turbo; a likely explanation is that the unpinned process migrates to whichever core has the highest instantaneous turbo frequency, but this remains an open measurement question. Both datasets are archived in \texttt{paper/data/} as \texttt{benchmark\_results.json} (unpinned) and \texttt{benchmark\_results\_pinned.json} (pinned).
    \item \textbf{Minimum run time:} Benchmarks were invoked with \texttt{--benchmark\_min\_time=0.3}, which sets the minimum \emph{measured} wall-clock time per family to 0.3 seconds. This is distinct from warm-up, which Google Benchmark disables by default (controlled separately by \texttt{--benchmark\_min\_warmup\_time}). Cold-cache effects are mitigated by the pre-filled warm book constructed before each timing region.
    \item \textbf{Repetitions:} 10--30 repetitions per benchmark family; p50 and p99 are computed over benchmark repetitions from Google Benchmark JSON output using \texttt{tools/plot\_bench\_suite.py}.
    \item \textbf{Book state:} a warm, pre-filled book with N resting orders (varied per benchmark) is set up before timing begins.
    \item \textbf{Allocation tracking:} a global allocation counter intercepts \texttt{operator new} and \texttt{operator delete}; `allocs/op' is reported for each benchmark to validate the allocation-free design of the hot path.
\end{itemize}

\subsection{Allocation Behaviour and the Hot-Path Guarantee}

MSIM is allocation-free on the critical single-order matching path:
\texttt{BM\_ProcessMarketOrder} reports \texttt{allocs/op = 0} across all
repetitions and book sizes ($N = 100$ to $N = 10{,}000$). This guarantee rests
on three design choices: \texttt{SmallVector<Trade,4>} stores up to four
\texttt{Trade} objects inline on the stack frame of \texttt{process()}---no
heap is touched for the $>$95\% of orders that generate four or fewer
fills; \texttt{FlatHashMap} is pre-allocated before the simulation loop via
\texttt{OrderBook::reserve()}.

Allocations occur only in three amortised cases. First, the
\texttt{SmallVector} spills to the heap when a sweep generates more than four
simultaneous trades: \texttt{BM\_ProcessMarket\_SweepKLevels} at $K=8$ reports
\texttt{allocs/op = 0.0103}, meaning one allocation per approximately
$\lceil 1/0.0103 \rceil \approx 97$ benchmark iterations---only when the
sweep is large enough to produce a fifth \texttt{Trade} object. At $K=1{,}024$,
\texttt{allocs/op} $\approx 6.43 \times 10^{-4}$, implying one allocation
per approximately $\lceil 1/(6.43\times10^{-4}) \rceil \approx 1{,}555$ sweep
calls. Second, \texttt{Level::q} triggers a vector reallocation the first time
more than its initial capacity of orders rest at the same price level;
this is amortised over subsequent insertions. Third,
\texttt{FlatHashMap::grow()} fires when the locator map exceeds 50\% load,
requiring $>128$ simultaneous resting orders with the default 256-slot capacity.

The correct characterisation is therefore: \emph{allocation-free on the common
hot path; amortised near-zero over a full simulation run with pre-reserved data
structures.}

\subsection{Benchmark Results}

\Cref{tab:fullbench} presents the complete benchmark suite results.

\begin{table}[H]
\centering
\caption{Full benchmark suite results. Windows, MSVC 19.44, Release, Intel Core i7-13700F (16 cores/24 threads) @ \SI{2100}{\mega\hertz}.}
\label{tab:fullbench}
\resizebox{\textwidth}{!}{%
\begin{tabular}{lrrrrl}
\toprule
Benchmark & p50 (ns) & p90 (ns) & p99 (ns) & Reps & Notes \\
\midrule
ProcessReject\_InvalidQty & 14.9 & 15.0 & 15.0 & 25 & Rule-layer reject only \\
ProcessMarketOrder & \textbf{46.6} & 47.0 & \textbf{47.1} & 75 & Market order hot path \\
ProcessCrossingLimitIOC & 46.8 & 47.4 & 48.0 & 30 & Crossing limit IOC \\
Throughput\_ProcessMarketOrder & 45.7 & 46.0 & 46.4 & 30 & Sustained throughput \\
ProcessMarket\_SweepKLevels & 26.3 & 26.6 & 26.9 & 275 & Per-level cost at K=1024 \\
BookDepth\_TopN & 84.0 & 86.9 & 88.6 & 200 & L2 depth snapshot \\
BookCancel\_O1 & 127.7 & 128.6 & 128.7 & 25 & Cancel resting order \\
BookAddRestingLimit & 150.2 & 153.2 & 155.1 & 30 & Add resting limit \\
BookModifyQty\_O1 & 221.8 & 223.6 & 224.5 & 25 & Reduce resting quantity \\
\midrule
\textbf{Throughput} & \multicolumn{5}{l}{21.9M market orders/sec at p50 $\cdot$ 21.6M/sec at p99} \\
\bottomrule
\end{tabular}%
}% end resizebox
\end{table}

\paragraph{Latency stability.} \texttt{BM\_ProcessMarketOrder} is parameterised by $N \in \{100, 1000, 10000\}$ resting orders in the book. The p50 latency is \SI{46.6}{\nano\second}, \SI{46.6}{\nano\second}, and \SI{46.8}{\nano\second} respectively---a variation of ${<}\,0.5\%$ over two orders of magnitude of book depth. This stability confirms that the FlatPriceMap and vector-queue design keeps all hot data (the best price level and its associated queue) in L1 cache regardless of total book depth.

\paragraph{Allocation-free hot path.} The \texttt{allocs/op} counter is zero for all benchmarks except \texttt{BM\_ProcessMarket\_SweepKLevels} at high $K$ values, where the lazy compaction of \texttt{FlatPriceMap} triggers an occasional vector reallocation. In the normal case ($K \leq 32$ per market order), the path is completely allocation-free.

\paragraph{Comparison with related systems.} The most defensible performance comparison is internal: the Phase~0$\to$Phase~8 progression on identical C++ code and hardware (\Cref{tab:optresults}) demonstrates 4--25$\times$ improvement on identical operations. ABIDES \citep{byrd2020} is the closest academic simulator in scope; a direct numerical throughput comparison is not meaningful because ABIDES measures end-to-end event processing including network and agent logic while MSIM's figure measures only the matching kernel. The two systems address complementary use cases. Finally, the 21.9M~ops/sec figure measures the isolated kernel; a full 300-second validation simulation generates $\sim$774 trades and completes in under 5 seconds wall-clock time. Agent \texttt{step()} computation---not matching latency---is therefore the practical bottleneck during simulation runs.

% ─────────────────────────────────────────────────────────────────────────────
\section{Conclusion}
\label{sec:conclusion}
% ─────────────────────────────────────────────────────────────────────────────

\subsection{Summary}

We have presented MSIM, a deterministic market microstructure simulator achieving \SI{46.6}{\nano\second} (p50) and \SI{47.1}{\nano\second} (p99) market order processing latency on commodity hardware, with a sustained throughput of 21.9 million order operations per second. Eight systematic optimisation passes transformed a textbook pointer-chasing implementation into a cache-resident, allocation-free hot path, reducing key latency metrics by up to 5.9$\times$ (book add), 7.0$\times$ (cancel), and 25.1$\times$ (depth query).

The central architectural finding is that the dominant cost in a high-performance limit order book is not algorithmic complexity but memory layout. The transition from node-based, pointer-chasing structures (\texttt{std::list}, \texttt{std::map}, \texttt{std::unordered\_map}) to contiguous-memory alternatives (\texttt{FlatPriceMap}, \texttt{FlatHashMap}, vector queues) reduced every major latency metric by at least 5$\times$. The two most impactful individual changes---O(1) front erase via \texttt{front\_offset} and O(1) depth via \texttt{live\_count}---each eliminated a linear scan that was hidden behind a constant-complexity interface in the baseline implementation.

On the economic side, a 300-second simulation with nine heterogeneous agents illustrates qualitative emergence of five canonical microstructure stylised facts simultaneously with the multi-agent validation configuration. Four statistics fall within the ranges reported in the primary empirical literature; price impact (Kyle $\lambda$) is emergent but not formally significant at this sample size, a limitation explained mechanically by the A-S market maker's continuous quote adjustment. The trade-sign autocorrelation (seed=42: 0.270; 50-seed median: 0.327) is consistent with \citet{bouchaud2004}'s empirical finding of 0.30--0.70 and arises as an emergent property of the Hawkes self-excitation mechanism and fundamental value agent behaviour, rather than being explicitly parameterised.

\subsection{Limitations}

Several limitations should be noted.

\paragraph{Catastrophic-path instability.} Across 50 seeds, Seed~1048 experienced a complete runaway: the mid-price collapsed from 10{,}000 to $-4{,}877{,}547$ ticks, the market maker accumulated $-4{,}694$ lots across 115{,}737 orders, and the simulation produced meaningless statistics. This represents a $\approx$2\,\% catastrophic instability rate. The mechanism is a positive feedback loop between the mean-reversion FV agents and the A-S market maker: extreme one-sided informed flow drives the maker's reservation price off the price band, which attracts further informed trading in the same direction. Safeguards such as agent position limits or a minimum tick-band on the reservation price would prevent this; both are deferred to future work.

The price impact estimates (Kyle's lambda, power-law exponent) require substantially larger samples for reliable estimation; a production-quality validation would combine multi-seed runs with formal calibration to LOBster data. The negative adverse selection component ($-26.51$ ticks) reflects the A-S market maker's continuous re-pricing after each fill, which attenuates post-trade drift; a passive quoting rule would likely produce positive adverse selection consistent with the standard Glosten-Milgrom prediction. The fundamental value agent implements a private-signal OU process that mean-reverts to a fixed anchor $\mu$ initialised once from the first observed mid-price---not to the current mid-price at each step. The agent is therefore a mean-reversion trader rather than a truly informed trader in the Glosten-Milgrom sense. A proper informed-trading implementation would use an exogenous value process independent of market prices. The current implementation nonetheless generates price discovery dynamics consistent with informed flow, as evidenced by the spread decomposition and agent PnL results.

The current implementation operates on a single asset and single venue; multi-asset correlated dynamics and cross-venue arbitrage are not modelled. The engine runs single-threaded by design for determinism; parallel multi-seed sweeps are supported by spawning independent process instances but not by internal multi-threading.

\subsection{Future Work}

\paragraph{Extended multi-seed validation.} The current 50-seed study (\Cref{subsec:multiseed}) confirms pass rates of 96--100\,\% across independent seeds. Future extensions include scaling to 500+ seeds, formal hypothesis testing, and calibration against empirical LOBster data.

\paragraph{Gymnasium wrapper for reinforcement learning.} Wrapping \texttt{World} as a \texttt{gym.Env} \citep{brockman2016} would enable RL agents (PPO: Proximal Policy Optimisation; SAC: Soft Actor-Critic; DQN: Deep Q-Network) to train directly against the MSIM engine. The deterministic seeding guarantees exact reproducibility across training runs, providing a controlled RL benchmark for market microstructure.

\paragraph{Zero-copy numpy output.} Exposing \texttt{trades}, \texttt{tops}, and \texttt{fills} as zero-copy numpy structured arrays would eliminate the Python list construction overhead in the DataFrame conversion methods, reducing post-processing time for large simulation runs.

\paragraph{Fuzz testing.} A LibFuzzer target covering the auction uncrossing path, FOK atomicity checks, and circuit breaker state transitions would provide stronger correctness guarantees than the current 26-test unit suite.

\paragraph{Calibration to empirical data.} Calibrating agent parameters to LOBster ITCH data \citep{huang2011} would enable direct comparison of simulated spread decomposition and price impact statistics against the empirical literature, transforming MSIM from a qualitative validator to a quantitative empirical research tool.

\paragraph{GPU parallelism.} CUDA kernels for agent \texttt{step()} computation would enable 1000-seed sweeps in seconds rather than minutes, unlocking the large-scale stochastic simulation workloads needed for statistical validation of microstructure theories.

% ─────────────────────────────────────────────────────────────────────────────
\begin{thebibliography}{99}

\bibitem[Ait-Sahalia and Jacod(2014)]{aitsahalia2014}
Ait-Sahalia, Y. and Jacod, J. (2014).
\textit{High-Frequency Financial Econometrics}.
Princeton University Press.

\bibitem[Almgren and Chriss(2001)]{almgren2001}
Almgren, R. and Chriss, N. (2001).
Optimal execution of portfolio transactions.
\textit{Journal of Risk}, 3(2), 5--39.

\bibitem[Almgren et al.(2005)]{almgren2005}
Almgren, R., Thum, C., Hauptmann, E. and Li, H. (2005).
Direct estimation of equity market impact.
\textit{Risk}, 18(7), 58--62.

\bibitem[Amihud(2002)]{amihud2002}
Amihud, Y. (2002).
Illiquidity and stock returns: Cross-section and time-series effects.
\textit{Journal of Financial Markets}, 5(1), 31--56.

\bibitem[Avellaneda and Stoikov(2008)]{avellaneda2008}
Avellaneda, M. and Stoikov, S. (2008).
High-frequency trading in a limit order book.
\textit{Quantitative Finance}, 8(3), 217--224.

\bibitem[Bollerslev(1986)]{bollerslev1986}
Bollerslev, T. (1986).
Generalized autoregressive conditional heteroskedasticity.
\textit{Journal of Econometrics}, 31(3), 307--327.

\bibitem[Bouchaud et al.(2004)]{bouchaud2004}
Bouchaud, J.-P., Gefen, Y., Potters, M. and Wyart, M. (2004).
Fluctuations and response in financial markets: The subtle nature of `random' price changes.
\textit{Quantitative Finance}, 4(2), 176--190.

\bibitem[Brockman et al.(2016)]{brockman2016}
Brockman, G., Cheung, V., Pettersson, L., Schneider, J., Schulman, J., Tang, J. and Zaremba, W. (2016).
OpenAI Gym.
\textit{arXiv preprint arXiv:1606.01540}.

\bibitem[Byrd et al.(2019)]{byrd2019}
Byrd, D., Karber, M., Hybinette, M. and Balch, T. H. (2019).
Step: Simulating and evaluating trading platforms.
\textit{Proceedings of the 2019 ACM SIGSIM Conference}, pp. 1--12.

\bibitem[Byrd et al.(2020)]{byrd2020}
Byrd, D., Hybinette, M. and Balch, T. H. (2020).
ABIDES: Towards high-fidelity multi-agent market simulation.
\textit{Proceedings of the 2020 ACM SIGSIM Conference on Principles of Advanced Discrete Simulation}, pp. 11--22.

\bibitem[Celis et al.(1985)]{celis1985}
Celis, P., Larson, P.-{\AA}. and Munro, J. I. (1985).
Robin Hood hashing.
\textit{Proceedings of the 26th Annual Symposium on Foundations of Computer Science (FOCS)}, pp. 281--288.

\bibitem[Cliff(2018)]{cliff2018}
Cliff, D. (2018).
BSE: A Minimal Simulation of a Limit-Order-Book Stock Exchange.
\textit{arXiv preprint arXiv:1809.06027}.

\bibitem[Cont(2001)]{cont2001}
Cont, R. (2001).
Empirical properties of asset returns: Stylised facts and statistical issues.
\textit{Quantitative Finance}, 1(2), 223--236.

\bibitem[Cont et al.(2014)]{cont2014}
Cont, R., Kukanov, A. and Stoikov, S. (2014).
The price impact of order book events.
\textit{Journal of Financial Econometrics}, 12(1), 47--88.

\bibitem[Demsetz(1968)]{demsetz1968}
Demsetz, H. (1968).
The cost of transacting.
\textit{Quarterly Journal of Economics}, 82(1), 33--53.

\bibitem[Engle(1982)]{engle1982}
Engle, R. F. (1982).
Autoregressive conditional heteroscedasticity with estimates of the variance of United Kingdom inflation.
\textit{Econometrica}, 50(4), 987--1007.

\bibitem[Fog(2024)]{fog2024}
Fog, A. (2024).
Instruction tables: Lists of instruction latencies, throughputs and micro-operation breakdowns for Intel, AMD, and VIA CPUs.
Available at \url{https://www.agner.org/optimize/instruction_tables.pdf}.

\bibitem[Glosten and Milgrom(1985)]{glosten1985}
Glosten, L. R. and Milgrom, P. R. (1985).
Bid, ask and transaction prices in a specialist market with heterogeneously informed traders.
\textit{Journal of Financial Economics}, 14(1), 71--100.

\bibitem[Glosten and Harris(1988)]{glosten1988}
Glosten, L. R. and Harris, L. E. (1988).
Estimating the components of the bid/ask spread.
\textit{Journal of Financial Economics}, 21(1), 123--142.

\bibitem[Grossman and Stiglitz(1980)]{grossman1980}
Grossman, S. J. and Stiglitz, J. E. (1980).
On the impossibility of informationally efficient markets.
\textit{American Economic Review}, 70(3), 393--408.

\bibitem[Hawkes(1971)]{hawkes1971}
Hawkes, A. G. (1971).
Spectra of some self-exciting and mutually exciting point processes.
\textit{Biometrika}, 58(1), 83--90.

\bibitem[Huang and Stoll(1997)]{huang1997}
Huang, R. D. and Stoll, H. R. (1997).
The components of the bid-ask spread: A general approach.
\textit{Review of Financial Studies}, 10(4), 995--1034.

\bibitem[Huang and Polak(2011)]{huang2011}
Huang, R. and Polak, T. (2011).
LOBSTER: Limit order book reconstruction system.
Working Paper, Humboldt University Berlin.

\bibitem[Intel(2024)]{intel2024}
Intel Corporation (2024).
\textit{Intel 64 and IA-32 Architectures Optimization Reference Manual}.
Document 248966-048. Available at \url{https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html}.

\bibitem[Jegadeesh and Titman(1993)]{jegadeesh1993}
Jegadeesh, N. and Titman, S. (1993).
Returns to buying winners and selling losers: Implications for stock market efficiency.
\textit{Journal of Finance}, 48(1), 65--91.

\bibitem[Kyle(1985)]{kyle1985}
Kyle, A. S. (1985).
Continuous auctions and insider trading.
\textit{Econometrica}, 53(6), 1315--1335.

\bibitem[Le Baron et al.(1999)]{lebaron1999}
LeBaron, B., Arthur, W. B. and Palmer, R. (1999).
Time series properties of an artificial stock market.
\textit{Journal of Economic Dynamics and Control}, 23(9--10), 1487--1516.

\bibitem[Mandelbrot(1963)]{mandelbrot1963}
Mandelbrot, B. (1963).
The variation of certain speculative prices.
\textit{Journal of Business}, 36(4), 394--419.

\bibitem[Masad and Kazil(2015)]{masad2015}
Masad, D. and Kazil, J. (2015).
Mesa: An agent-based modeling framework.
\textit{Proceedings of the 14th Python in Science Conference}, pp. 53--60.

\bibitem[Murphy(1999)]{murphy1999}
Murphy, J. J. (1999).
\textit{Technical Analysis of the Financial Markets}.
New York Institute of Finance.

\bibitem[Roll(1984)]{roll1984}
Roll, R. (1984).
A simple implicit measure of the effective bid-ask spread in an efficient market.
\textit{Journal of Finance}, 39(4), 1127--1139.

\bibitem[Steele et al.(2014)]{steele2014}
Steele, G. L., Lea, D. and Flood, C. H. (2014).
Fast splittable pseudorandom number generators.
\textit{Proceedings of the 2014 ACM International Conference on Object Oriented
Programming Systems Languages \& Applications (OOPSLA)}, pp.\ 453--472.



\bibitem[Steele and Vigna(2021)]{steele2021}
Steele, G. L. and Vigna, S. (2021).
Computationally easy, spectrally good multipliers for congruential pseudorandom number generators.
\textit{Software: Practice and Experience}, 52(2), 443--458.



% ─────────────────────────────────────────────────────────────────────────────
\appendix
% ─────────────────────────────────────────────────────────────────────────────

\section{Complete Benchmark Results}
\label{app:benchmarks}

\Cref{tab:app_sweep} presents the full sweep results for \texttt{BM\_ProcessMarket\_SweepKLevels} across all tested values of $K$.

\begin{table}[H]
\centering
\caption{ProcessMarket\_SweepKLevels: per-level latency across $K$ values after all optimisation passes (50 reps each). Windows, MSVC 19.44, Release. The K=1024 result (\textbf{26.3\,ns} p50, bold) is the value reported in \Cref{tab:fullbench}; all values confirm the front\_offset O(1) erase implemented in Phase~3.}
\label{tab:app_sweep}
\begin{tabular}{rrrr}
\toprule
$K$ (levels swept) & p50 (ns) & p99 (ns) & allocs/op \\
\midrule
1    & 26.5 & 27.1 & 0 \\
2    & 26.3 & 26.8 & 0 \\
4    & 26.3 & 26.7 & 0 \\
8    & 26.4 & 26.8 & $1.02 \times 10^{-2}$ \\
16   & 26.3 & 26.7 & $1.02 \times 10^{-2}$ \\
32   & 26.3 & 26.6 & $7.68 \times 10^{-3}$ \\
64   & 26.3 & 26.7 & $5.13 \times 10^{-3}$ \\
128  & 26.3 & 26.9 & $3.20 \times 10^{-3}$ \\
256  & 26.4 & 26.8 & $1.92 \times 10^{-3}$ \\
512  & 26.3 & 26.8 & $1.12 \times 10^{-3}$ \\
1024 & \textbf{26.3} & \textbf{26.5} & $6.41 \times 10^{-4}$ \\
\bottomrule
\end{tabular}
\begin{minipage}{0.9\linewidth}\small
\textit{Note:} The decreasing \texttt{allocs/op} as $K$ grows reflects amortisation: the \texttt{SmallVector} heap-allocation cost is spread over more price levels per operation, not a reduction in absolute allocations.
\end{minipage}

\end{table}

\section{Reproducibility}
\label{app:repro}

All numerical results in this paper are reproducible from the public repository
at commit \texttt{da38eaac7712f2ba352f34a37820324be7b8cc2e} (tag \texttt{v2.1.1}).
A \texttt{Provenance.json} file in the repo root records the complete build
environment and data-generation commands.

\paragraph{Build environment.}
\begin{itemize}[noitemsep]
    \item \textbf{OS:} Windows 11 (benchmark machine); also verified on Ubuntu 22.04 and macOS 14
    \item \textbf{Compiler:} MSVC 19.44 (Windows); GCC 12 / Clang 14 (Linux/macOS)
    \item \textbf{Build flags:} \texttt{-DCMAKE\_BUILD\_TYPE=Release -DMSIM\_BUILD\_PYTHON=ON}
    \item \textbf{Hardware:} Intel Core i7-13700F (8P + 8E cores, 24 threads), base clock \SI{2100}{\mega\hertz}
    \item \textbf{Python:} 3.11; dependencies: \texttt{numpy}, \texttt{pandas}, \texttt{scipy}
\end{itemize}

\begin{verbatim}
git clone https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-
git checkout v2.1.1

# Windows (x64 Native Tools Command Prompt for VS 2022):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON ^
      -G "NMake Makefiles"
cmake --build build

# macOS / Linux:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON
cmake --build build
\end{verbatim}

\paragraph{Benchmarks.}
\begin{verbatim}
build\msim_bench.exe --benchmark_min_time=0.3 --benchmark_format=json ^
    --benchmark_out=paper\data\benchmark_results.json
\end{verbatim}
Primary results (Table~\ref{tab:fullbench}) use unpinned runs with turbo boost active.
A supplementary pinned run (\texttt{ProcessorAffinity = 1}, P-core~0) is archived
as \texttt{paper/data/benchmark\_results\_pinned.json}; pinned latencies are 10--35\,\% higher.

\paragraph{Stylised facts and bootstrap CIs (seed = 42).}
\begin{verbatim}
set PYTHONPATH=%CD%\src\python
python src\python\run_one_seed.py    # prints seed-42 metrics to stdout
python src\python\bootstrap_ci.py   # writes paper/data/bootstrap_results.json
\end{verbatim}
Output: \texttt{paper/data/bootstrap\_results.json} (Table~\ref{tab:sfvalidation}).

\paragraph{Multi-seed robustness study (50 seeds).}
\begin{verbatim}
python src\python\multiseed_study.py
\end{verbatim}
Output: \texttt{paper/data/multiseed\_results.csv} and
\texttt{paper/data/multiseed\_summary.json} (Table~\ref{tab:multiseed}).
Seeds 1029 and 1048 are excluded; rationale is documented in the table caption.

\paragraph{PnL conservation audit.}
\begin{verbatim}
python src\python\pnl_conservation_check.py
\end{verbatim}
Confirms total mark-to-market PnL and net position sum to zero across all agents.

\paragraph{Spread sensitivity (Table~\ref{tab:spread_sensitivity}).}
Generated by \texttt{src/python/measure\_3e\_spread\_sensitivity.py} with Python
bindings built (\texttt{MSIM\_BUILD\_PYTHON=ON}) and \texttt{PYTHONPATH=src/python}.
Output is printed to stdout; the values in Table~\ref{tab:spread_sensitivity} were
transcribed from that output.


\section{Agent Configuration Reference}
\label{app:api}

\Cref{tab:app_configs} lists the agent parameters used in the validation experiment,
distinguishing script overrides from inherited code defaults.

\begin{table}[H]
\centering
\caption{Agent parameters used in the validation experiment
(Section~\ref{sec:validation}), distinguishing explicit
\emph{script overrides} from inherited \emph{code defaults}.
The IS agent is not part of the 9-agent validation ecology and is omitted.}
\label{tab:app_configs}
\resizebox{\textwidth}{!}{%
\begin{tabular}{llllp{4.5cm}}
\toprule
Agent & Parameter & Value & Source & Description \\
\midrule
\multirow{4}{*}{HawkesNoise (×5)} & \texttt{p\_market} & 0.5 & script & Fraction of market orders \\
 & \texttt{imbalance\_bias} & 0.3 & default & Imbalance sensitivity $\xi$ \\
 & \texttt{lot\_size} & 2 & script & Order quantity \\
 & \texttt{min/max\_offset} & 1/5 & default & Limit price offset range (ticks) \\
\midrule
\multirow{6}{*}{MarketMakerAS (×1)} & \texttt{gamma} & 0.01 & default & Risk aversion $\gamma$ \\
 & \texttt{kappa} & 1.5 & default & Fill-rate sensitivity $\kappa$ \\
 & \texttt{sigma\_init} & 2.0 & default & Initial volatility estimate \\
 & \texttt{sigma\_ewma} & 0.02 & default & EWMA decay $\alpha_{\text{ewma}}$ \\
 & \texttt{alpha\_imb} & 0.5 & default & Imbalance skew $\alpha_{\text{imb}}$ \\
 & \texttt{min\_half\_spread\_ticks} & 1 & default & Minimum half-spread $\delta_{\min}$ \\
\midrule
\multirow{4}{*}{FundamentalValue-A (×1)} & \texttt{kappa} & 0.005 & default & OU mean-reversion speed $\kappa$ \\
 & \texttt{sigma\_v} & 0.3 & script & Value process volatility $\sigma_V$ \\
 & \texttt{threshold} & 1.0 & script & Trading threshold $\theta$ (ticks) \\
 & \texttt{lot\_size} & 2 & script & Order quantity (aggressive) \\
\midrule
\multirow{4}{*}{FundamentalValue-B (×1)} & \texttt{kappa} & 0.005 & default & OU mean-reversion speed $\kappa$ \\
 & \texttt{sigma\_v} & 0.2 & script & Value process volatility $\sigma_V$ \\
 & \texttt{threshold} & 3.0 & script & Trading threshold $\theta$ (ticks) \\
 & \texttt{lot\_size} & 1 & script & Order quantity (conservative) \\
\midrule
\multirow{6}{*}{Momentum (×1)} & \texttt{alpha\_fast} & 2/6\,$\approx$\,0.333 & default & Fast EMA decay $\alpha^f$ \\
 & \texttt{alpha\_slow} & 2/21\,$\approx$\,0.095 & default & Slow EMA decay $\alpha^s$ \\
 & \texttt{entry\_band} & 2.0 & script & Signal threshold $\delta_{\text{entry}}$ (ticks) \\
 & \texttt{exit\_band} & 0.05 & default & Exit threshold $\delta_{\text{exit}}$ (ticks) \\
 & \texttt{lot\_size} & 1 & script & Order quantity \\
 & \texttt{max\_position} & 15 & default & Maximum inventory $Q_{\max}$ \\
\bottomrule
\end{tabular}%
}% end resizebox
\end{table}

\bibitem[Thompson et al.(2011)]{thompson2011}
Thompson, M., Farley, D., Barker, M., Gee, P. and Stewart, A. (2011).
LMAX Disruptor: High performance alternative to bounded queues for exchanging data between concurrent threads.
LMAX Technical Report.

\bibitem[Vach(2015)]{vach2015}
Vach, D. (2015).
Comparison of double auction mechanisms.
\textit{Working Paper}.


\end{thebibliography}
\end{document}
