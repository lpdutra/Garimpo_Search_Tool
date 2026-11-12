// Renders the app icon as an .iconset folder: swift scripts/make-icon.swift <output.iconset>
import AppKit

let output = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first ?? "AppIcon.iconset")
try? FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

func render(size: CGFloat) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size), pixelsHigh: Int(size), bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let scale = size / 1024

    // Rounded square following the macOS icon grid.
    let tile = NSRect(x: 100 * scale, y: 100 * scale, width: 824 * scale, height: 824 * scale)
    let shape = NSBezierPath(roundedRect: tile, xRadius: 185 * scale, yRadius: 185 * scale)
    NSGradient(colors: [NSColor(red: 0.13, green: 0.16, blue: 0.22, alpha: 1),
                        NSColor(red: 0.05, green: 0.07, blue: 0.11, alpha: 1)])!.draw(in: shape, angle: -90)

    // Gold nuggets.
    let gold = NSGradient(colors: [NSColor(red: 1.0, green: 0.86, blue: 0.35, alpha: 1),
                                   NSColor(red: 0.85, green: 0.58, blue: 0.10, alpha: 1)])!
    for (x, y, r) in [(700.0, 250.0, 62.0), (800.0, 300.0, 40.0), (600.0, 215.0, 34.0)] {
        let nugget = NSBezierPath(ovalIn: NSRect(x: (x - r) * scale, y: (y - r * 0.8) * scale, width: 2 * r * scale, height: 1.6 * r * scale))
        gold.draw(in: nugget, angle: 60)
    }

    // Magnifying glass.
    let lensCenter = NSPoint(x: 560 * scale, y: 600 * scale)
    let lensRadius = 190 * scale
    let handle = NSBezierPath()
    handle.move(to: NSPoint(x: lensCenter.x - lensRadius * 0.72, y: lensCenter.y - lensRadius * 0.72))
    handle.line(to: NSPoint(x: 260 * scale, y: 300 * scale))
    handle.lineWidth = 70 * scale
    handle.lineCapStyle = .round
    NSColor(red: 0.85, green: 0.58, blue: 0.10, alpha: 1).setStroke()
    handle.stroke()

    let lens = NSBezierPath(ovalIn: NSRect(x: lensCenter.x - lensRadius, y: lensCenter.y - lensRadius,
                                           width: lensRadius * 2, height: lensRadius * 2))
    NSColor(white: 1, alpha: 0.12).setFill()
    lens.fill()
    lens.lineWidth = 46 * scale
    NSColor(red: 1.0, green: 0.80, blue: 0.30, alpha: 1).setStroke()
    lens.stroke()

    let shine = NSBezierPath()
    shine.appendArc(withCenter: lensCenter, radius: lensRadius * 0.68, startAngle: 105, endAngle: 165)
    shine.lineWidth = 22 * scale
    shine.lineCapStyle = .round
    NSColor(white: 1, alpha: 0.55).setStroke()
    shine.stroke()

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for base in [16, 32, 128, 256, 512] {
    for factor in [1, 2] {
        let name = factor == 1 ? "icon_\(base)x\(base).png" : "icon_\(base)x\(base)@2x.png"
        try render(size: CGFloat(base * factor)).write(to: output.appendingPathComponent(name))
    }
}
