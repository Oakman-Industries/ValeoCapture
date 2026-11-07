// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import AppKit

/// Launch screen with the look of the repository banner (assets/banner.svg):
/// dark diagonal gradient, a Bayer mosaic fading in towards the right, green
/// accent lines, the app icon, name, tagline and feature chips. Drawn with
/// AppKit, so it needs no SVG support (the app runs on macOS 10.15.7+).
final class LaunchSplashView: NSView {
    // Banner geometry is 1280 x 360; the splash draws it at `scale`.
    static let bannerSize = NSSize(width: 1280, height: 360)
    private let scale: CGFloat
    init(scale: CGFloat) {
        self.scale = scale
        super.init(frame: NSRect(x: 0, y: 0, width: Self.bannerSize.width * scale, height: Self.bannerSize.height * scale))
    }
    required init?(coder: NSCoder) { fatalError("not used") }
    override var isFlipped: Bool { true }

    private static func colour(_ hex: UInt32, _ alpha: CGFloat = 1) -> NSColor {
        NSColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
                blue: CGFloat(hex & 0xff) / 255, alpha: alpha)
    }

    override func draw(_ dirtyRect: NSRect) {
        let s = scale, w = bounds.width, h = bounds.height
        NSBezierPath(roundedRect: bounds, xRadius: 18 * s, yRadius: 18 * s).addClip()
        // Background: #111417 top-left to #23292f bottom-right.
        NSGradient(starting: Self.colour(0x111417), ending: Self.colour(0x23292f))!
            .draw(in: bounds, angle: atan2(h, w) * 180 / .pi)
        // Bayer mosaic (R G / G B on a 28-unit cell), transparent up to 45 % of the
        // width, then rising to 0.55 x 0.55 opacity at the right edge.
        let cell = 28 * s, square = 12 * s, inset = 1 * s, step = 14 * s
        let tiles: [(CGFloat, CGFloat, UInt32)] = [(0, 0, 0xe5484d), (step, 0, 0x46a758), (0, step, 0x46a758), (step, step, 0x3e63dd)]
        var x: CGFloat = 0
        while x < w {
            // Squared ramp: the mask is linear, but over the dark gradient the
            // mosaic reads as starting much later, as in the rendered banner.
            let ramp = max(0, (x + cell / 2) / w - 0.45) / 0.55
            let fade = ramp * ramp * 0.55 * 0.55
            if fade > 0 {
                var y: CGFloat = 0
                while y < h {
                    for (dx, dy, hex) in tiles {
                        Self.colour(hex, fade).setFill()
                        NSBezierPath(roundedRect: NSRect(x: x + dx + inset, y: y + dy + inset, width: square, height: square),
                                     xRadius: 2 * s, yRadius: 2 * s).fill()
                    }
                    y += cell
                }
            }
            x += cell
        }
        // Accent lines: a soft green glow along the top, a solid bar along the bottom.
        NSGradient(starting: Self.colour(0x6e8f69, 0.35), ending: Self.colour(0x6e8f69, 0))!
            .draw(in: NSRect(x: 0, y: 0, width: w, height: 6 * s), angle: 90)
        Self.colour(0x6e8f69, 0.8).setFill(); NSRect(x: 0, y: h - 4 * s, width: w, height: 4 * s).fill()

        // App icon.
        if let icon = NSApp.applicationIconImage ?? NSImage(named: NSImage.applicationIconName) {
            icon.draw(in: NSRect(x: 64 * s, y: 74 * s, width: 196 * s, height: 196 * s), from: .zero, operation: .sourceOver,
                      fraction: 1, respectFlipped: true, hints: [.interpolation: NSImageInterpolation.high])
        }
        // Text, placed by baseline as in the banner.
        func text(_ string: String, x: CGFloat, baseline: CGFloat, size: CGFloat, weight: NSFont.Weight, hex: UInt32, kern: CGFloat = 0) {
            let font = NSFont.systemFont(ofSize: size * s, weight: weight)
            let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: Self.colour(hex), .kern: kern * s]
            (string as NSString).draw(at: NSPoint(x: x * s, y: baseline * s - font.ascender), withAttributes: attributes)
        }
        text("ValeoCapture", x: 300, baseline: 138, size: 64, weight: .bold, hex: 0xf4f6f8, kern: -1)
        text("Native macOS capture for the Leaf Valeo 11 digital back", x: 302, baseline: 182, size: 24, weight: .regular, hex: 0xc3cbd3)
        text("Keeps a discontinued back shooting on current Macs — raw kept, editable files out.", x: 302, baseline: 222, size: 17,
             weight: .regular, hex: 0x8d98a3)
        // Feature chips.
        let chips: [(String, CGFloat, CGFloat)] = [("Physical-shutter capture", 300, 233), ("Editable DNG + 16-bit TIFF", 545, 250),
                                                   ("Intel + Apple Silicon", 807, 208), ("Tested on macOS 15 & 26", 1027, 225)]
        for (label, cx, cw) in chips {
            let rect = NSRect(x: cx * s, y: 262 * s, width: cw * s, height: 34 * s)
            let pill = NSBezierPath(roundedRect: rect, xRadius: 17 * s, yRadius: 17 * s)
            NSColor(white: 1, alpha: 0.07).setFill(); pill.fill()
            NSColor(white: 1, alpha: 0.16).setStroke(); pill.lineWidth = max(1, s); pill.stroke()
            let font = NSFont.systemFont(ofSize: 14 * s)
            let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: Self.colour(0xd7dde3)]
            let size = (label as NSString).size(withAttributes: attributes)
            (label as NSString).draw(at: NSPoint(x: rect.midX - size.width / 2, y: rect.minY + 22.5 * s - font.ascender), withAttributes: attributes)
        }
    }
}

/// A borderless, centred window that shows the launch screen and fades out.
final class LaunchSplash {
    private let window: NSWindow
    init(scale: CGFloat = 0.75) {
        let view = LaunchSplashView(scale: scale)
        window = NSWindow(contentRect: view.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = true
        window.level = .floating; window.isReleasedWhenClosed = false
        window.contentView = view; window.center()
    }
    func show() { window.alphaValue = 1; window.orderFrontRegardless() }
    func dismiss(then completion: @escaping () -> Void) {
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.35; window.animator().alphaValue = 0
        }, completionHandler: { [window] in window.orderOut(nil); completion() })
    }

    /// Renders the launch screen to a PNG (development and documentation).
    static func render(to url: URL, scale: CGFloat = 1) -> Bool {
        let view = LaunchSplashView(scale: scale)
        guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return false }
        view.cacheDisplay(in: view.bounds, to: rep)
        guard let png = rep.representation(using: .png, properties: [:]) else { return false }
        return (try? png.write(to: url, options: .withoutOverwriting)) != nil
    }
}
