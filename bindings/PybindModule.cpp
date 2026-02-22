#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "OptionParams.h"
#include "IPricer.h"
#include "BlackScholesPricer.h"
#include "MonteCarloPricer.h"
#include "market_data/OrderBook.h"
#include "market_data/MarketDataClient.h"
#include "market_data/LivePricer.h"

namespace py = pybind11;

// ── Convenience factory ────────────────────────────────────────────────────────
// Creates a fully wired (OrderBook + MarketDataClient + LivePricer) set and
// returns them as a Python tuple(client, pricer).  The OrderBook is kept alive
// via shared_ptr shared between both objects.
static std::pair<std::shared_ptr<MarketDataClient>, std::shared_ptr<LivePricer>>
make_live_engine(const std::string& symbol = "btcusdt") {
    auto book   = std::make_shared<OrderBook>();
    auto client = std::make_shared<MarketDataClient>(book, symbol);
    auto pricer = std::make_shared<LivePricer>(book);
    return {client, pricer};
}

PYBIND11_MODULE(quant_pricer, m) {
    m.doc() = "Quant Pricing Engine — High-performance C++ options pricer exposed to Python.";

    // ── OptionType ────────────────────────────────────────────────────────────
    py::enum_<OptionType>(m, "OptionType")
        .value("Call", OptionType::Call)
        .value("Put",  OptionType::Put)
        .export_values();

    // ── OptionParams ──────────────────────────────────────────────────────────
    py::class_<OptionParams>(m, "OptionParams")
        .def(py::init<double, double, double, double, double, OptionType>(),
             py::arg("S"), py::arg("K"), py::arg("T"),
             py::arg("r"), py::arg("v"), py::arg("type"))
        .def_readwrite("S",    &OptionParams::S)
        .def_readwrite("K",    &OptionParams::K)
        .def_readwrite("T",    &OptionParams::T)
        .def_readwrite("r",    &OptionParams::r)
        .def_readwrite("v",    &OptionParams::v)
        .def_readwrite("type", &OptionParams::type)
        .def("__repr__", [](const OptionParams& p) {
            return "OptionParams(S=" + std::to_string(p.S)
                 + ", K="    + std::to_string(p.K)
                 + ", T="    + std::to_string(p.T)
                 + ", r="    + std::to_string(p.r)
                 + ", v="    + std::to_string(p.v)
                 + ", type=" + (p.type == OptionType::Call ? "Call" : "Put") + ")";
        });

    // ── PricingResult ─────────────────────────────────────────────────────────
    py::class_<PricingResult>(m, "PricingResult")
        .def_readonly("price",     &PricingResult::price)
        .def_readonly("std_error", &PricingResult::std_error)
        .def("__repr__", [](const PricingResult& r) {
            return "PricingResult(price="     + std::to_string(r.price)
                 + ", std_error=" + std::to_string(r.std_error) + ")";
        });

    // ── BlackScholesPricer ────────────────────────────────────────────────────
    py::class_<BlackScholesPricer>(m, "BlackScholesPricer")
        .def(py::init<>())
        .def("price", &BlackScholesPricer::price, py::arg("params"))
        .def("delta", &BlackScholesPricer::delta, py::arg("params"))
        .def("gamma", &BlackScholesPricer::gamma, py::arg("params"))
        .def("vega",  &BlackScholesPricer::vega,  py::arg("params"))
        .def("theta", &BlackScholesPricer::theta, py::arg("params"))
        .def("rho",   &BlackScholesPricer::rho,   py::arg("params"));

    // ── MonteCarloPricer ──────────────────────────────────────────────────────
    py::class_<MonteCarloPricer>(m, "MonteCarloPricer")
        .def(py::init<uint64_t, std::optional<uint64_t>>(),
             py::arg("num_paths"), py::arg("seed") = std::nullopt)
        .def("price",     &MonteCarloPricer::price,     py::arg("params"))
        .def("num_paths", &MonteCarloPricer::num_paths);

    // ── market_data submodule ─────────────────────────────────────────────────
    py::module_ md = m.def_submodule("market_data",
        "Real-time LOB market data feed and live option pricer.");

    // LiveOptionResult
    py::class_<LiveOptionResult>(md, "LiveOptionResult")
        .def_readonly("spot",         &LiveOptionResult::spot)
        .def_readonly("option_price", &LiveOptionResult::option_price)
        .def_readonly("best_bid",     &LiveOptionResult::best_bid)
        .def_readonly("best_ask",     &LiveOptionResult::best_ask)
        .def_readonly("spread",       &LiveOptionResult::spread)
        .def("__repr__", [](const LiveOptionResult& r) {
            return "LiveOptionResult(spot="   + std::to_string(r.spot)
                 + ", option="  + std::to_string(r.option_price)
                 + ", bid="     + std::to_string(r.best_bid)
                 + ", ask="     + std::to_string(r.best_ask)
                 + ", spread="  + std::to_string(r.spread) + ")";
        });

    // OrderBook
    py::class_<OrderBook, std::shared_ptr<OrderBook>>(md, "OrderBook")
        .def(py::init<>())
        .def("update_level", &OrderBook::updateLevel,
             py::arg("price"), py::arg("qty"), py::arg("is_bid"))
        .def_property_readonly("best_bid",  &OrderBook::getBestBid)
        .def_property_readonly("best_ask",  &OrderBook::getBestAsk)
        .def_property_readonly("mid_price", &OrderBook::getMidPrice);

    // MarketDataClient
    // start() releases the GIL so Python threads aren't blocked while the
    // C++ networking thread initialises.
    py::class_<MarketDataClient, std::shared_ptr<MarketDataClient>>(
            md, "MarketDataClient")
        .def(py::init([](std::shared_ptr<OrderBook> book,
                         const std::string& symbol) {
                 return std::make_shared<MarketDataClient>(book, symbol);
             }),
             py::arg("book"), py::arg("symbol") = "btcusdt")
        .def("start",      &MarketDataClient::start,
             py::call_guard<py::gil_scoped_release>())
        .def("stop",       &MarketDataClient::stop,
             py::call_guard<py::gil_scoped_release>())
        .def_property_readonly("is_running", &MarketDataClient::isRunning)
        .def_property_readonly("symbol",     &MarketDataClient::symbol);

    // LivePricer
    py::class_<LivePricer, std::shared_ptr<LivePricer>>(md, "LivePricer")
        .def(py::init([](std::shared_ptr<OrderBook> book) {
                 return std::make_shared<LivePricer>(book);
             }),
             py::arg("book"))
        .def("get_live_option_price",
             &LivePricer::getLiveOptionPrice,
             py::arg("strike"),
             py::arg("time_to_maturity"),
             py::arg("risk_free_rate"),
             py::arg("volatility"),
             py::arg("is_call") = true,
             py::call_guard<py::gil_scoped_release>());

    // make_live_engine() — convenience factory
    md.def("make_live_engine",
           &make_live_engine,
           py::arg("symbol") = "btcusdt",
           "Create a wired (MarketDataClient, LivePricer) pair sharing one OrderBook.\n"
           "Returns: tuple(client, pricer)");
}
