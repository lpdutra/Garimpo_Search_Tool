// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Garimpo",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "Garimpo", targets: ["GarimpoApp"]),
        .executable(name: "garimpo-cli", targets: ["GarimpoCLI"]),
        .library(name: "GarimpoCore", targets: ["GarimpoCore"]),
    ],
    targets: [
        .target(name: "GarimpoCore"),
        .executableTarget(name: "GarimpoApp", dependencies: ["GarimpoCore"]),
        .executableTarget(name: "GarimpoCLI", dependencies: ["GarimpoCore"]),
        .testTarget(name: "GarimpoCoreTests", dependencies: ["GarimpoCore"]),
    ],
    swiftLanguageModes: [.v5]
)
