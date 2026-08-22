// swift-tools-version:5.7
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Mark Liversedge
//
// Plan A5: PinPointCapture consumes libppcp as a SwiftPM package, so this
// exposes one C target named `CPPCP` — headers from `include/`, sources from
// `src/`, no module map to hand-maintain.  `Packages/Core` (CaptureCore) gains
// a dependency on it and wraps it in Swift; the layer-purity test there still
// holds, because CPPCP is not a platform framework.
//
// The target path is the repository root with an explicit `sources` list, so
// docs/, tests/ and tools/ are simply not part of the target.  `tests/` in
// particular must stay out: it holds C `main()` functions and would collide.

import PackageDescription

let package = Package(
    name: "CPPCP",
    products: [
        .library(name: "CPPCP", targets: ["CPPCP"])
    ],
    targets: [
        .target(
            name: "CPPCP",
            path: ".",
            exclude: ["docs", "tests", "tools", "build", "CMakeLists.txt",
                      "CMakePresets.json", "README.md", "LICENSE"],
            sources: ["src"],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include")
            ]
        )
    ]
)
