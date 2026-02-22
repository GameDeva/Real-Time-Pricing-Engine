#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "OptionParams.h"
#include "IPricer.h"
#include "BlackScholesPricer.h"
#include "MonteCarloPricer.h"

namespace py = pybind11;

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
}
