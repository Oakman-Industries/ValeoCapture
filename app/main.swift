// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import AppKit
import Foundation
import Darwin
import ImageIO

struct BundledLeafProfile {
    let name: String
    let url: URL
    static func load() -> [BundledLeafProfile] {
        guard let directory = Bundle.main.resourceURL?.appendingPathComponent("LeafProfiles", isDirectory: true),
              let file = try? FileHandle(forReadingFrom: directory.appendingPathComponent("manifest.json")) else { return [] }
        defer { file.closeFile() }
        let data = file.readData(ofLength: 128 * 1024 + 1)
        guard data.count <= 128 * 1024,
              let manifest = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              manifest["schema"] as? Int == 1,
              let rows = manifest["profiles"] as? [[String: Any]], rows.count <= 128 else { return [] }
        var seen = Set<String>()
        return rows.compactMap { row in
            guard let name = row["name"] as? String, name.count <= 128,
                  let filename = row["file"] as? String, filename.count <= 128,
                  filename == (filename as NSString).lastPathComponent,
                  ["icc", "icm"].contains((filename as NSString).pathExtension.lowercased()),
                  seen.insert(filename).inserted else { return nil }
            return BundledLeafProfile(name: name, url: directory.appendingPathComponent(filename))
        }
    }
}

final class CaptureClient {
    private var connection: NSXPCConnection?
    func call(_ action: @escaping (ValeoCaptureServiceProtocol, @escaping (String) -> Void) -> Void,
              completion: @escaping (String) -> Void) {
        if connection == nil {
            let c = NSXPCConnection(serviceName: captureServiceName)
            c.remoteObjectInterface = NSXPCInterface(with: ValeoCaptureServiceProtocol.self)
            c.interruptionHandler = { [weak self] in self?.connection = nil }
            c.invalidationHandler = { [weak self] in self?.connection = nil }
            c.resume(); connection = c
        }
        guard let proxy = connection?.remoteObjectProxyWithErrorHandler({ error in
            let value: [String: Any] = ["schema": 1, "ok": false, "error": error.localizedDescription]
            let json = try? JSONSerialization.data(withJSONObject: value)
            completion(json.flatMap { String(data: $0, encoding: .utf8) } ?? "{}")
        }) as? ValeoCaptureServiceProtocol else { return }
        action(proxy, completion)
    }
    deinit { connection?.invalidate() }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let client = CaptureClient()
    private var window: NSWindow!
    private let status = NSTextField(wrappingLabelWithString: "Looking for a Leaf Valeo 11…")
    private let detail = NSTextField(wrappingLabelWithString: "")
    private let destination = NSTextField(labelWithString: "Choose a capture folder")
    private let profile = NSTextField(labelWithString: "No Leaf ICC profile imported")
    private let preview = NSImageView()
    private let orientation = NSPopUpButton()
    private let iso = NSPopUpButton()
    private let softwareISOValues = [25, 50, 100, 200]
    private let leafPresets = NSPopUpButton()
    private var bundledProfiles: [BundledLeafProfile] = []
    private let tiff = NSButton(checkboxWithTitle: "Also save a 16-bit rendered TIFF", target: nil, action: nil)
    private var registryID: String?
    private var loginButton: NSButton!
    private var destinationURL: URL?
    private let calibration = CameraCalibration.bundled()
    private var cameraReady = false
    private var capturing = false
    private var startingCapture = false
    private var stopRequested = false
    private var stateRequestPending = false
    private var terminating = false
    private var profileLoading = false
    private var captureButton: NSButton!
    private var stopButton: NSButton!
    private let progress = NSProgressIndicator()
    private let captureStep = NSTextField(labelWithString: "Ready when you are")
    private let exportStatus = NSTextField(labelWithString: "DNG is always saved. TIFF uses your selected Leaf preset.")
    private let lastTiming = NSTextField(labelWithString: "")
    private let previewName = NSTextField(labelWithString: "No photographs yet")
    private let previewPlaceholder = NSTextField(wrappingLabelWithString: "Your next photograph appears here.\nOriginal raw files are kept with every capture.")
    private var revealButton: NSButton!
    private var configurationControls: [NSControl] = []
    private var lastPreviewPath = ""
    private var captureTimer: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        let menu = NSMenu()
        let appMenuItem = NSMenuItem(); menu.addItem(appMenuItem)
        let appMenu = NSMenu(); appMenuItem.submenu = appMenu
        appMenu.addItem(withTitle: "Quit ValeoCapture", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        let developer = NSMenuItem(title: "Development", action: nil, keyEquivalent: "")
        let developmentMenu = NSMenu(); developer.submenu = developmentMenu; menu.addItem(developer)
        let demo = developmentMenu.addItem(withTitle: "Export Synthetic DNG…", action: #selector(exportSynthetic), keyEquivalent: "")
        demo.target = self; NSApp.mainMenu = menu

        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 920, height: 800),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "ValeoCapture"
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = NSColor(calibratedRed: 0.10, green: 0.11, blue: 0.12, alpha: 1)
        window.center()
        let root = NSStackView(); root.orientation = .vertical; root.alignment = .leading; root.spacing = 16
        root.edgeInsets = NSEdgeInsets(top: 22, left: 24, bottom: 20, right: 24)
        root.translatesAutoresizingMaskIntoConstraints = false
        window.contentView!.addSubview(root)
        NSLayoutConstraint.activate([root.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor),
                                     root.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor),
                                     root.topAnchor.constraint(equalTo: window.contentView!.topAnchor),
                                     root.bottomAnchor.constraint(equalTo: window.contentView!.bottomAnchor)])
        func fullWidth(_ view: NSView) { view.widthAnchor.constraint(equalTo: root.widthAnchor, constant: -48).isActive = true }
        func caption(_ text: String) -> NSTextField {
            let label = NSTextField(labelWithString: text); label.font = NSFont.systemFont(ofSize: 10, weight: .semibold)
            label.textColor = .secondaryLabelColor; return label
        }
        func panel() -> NSStackView {
            let p = NSStackView(); p.orientation = .vertical; p.alignment = .leading; p.spacing = 9
            p.edgeInsets = NSEdgeInsets(top: 18, left: 18, bottom: 18, right: 18)
            p.wantsLayer = true; p.layer?.cornerRadius = 14
            p.layer?.backgroundColor = NSColor(calibratedRed: 0.15, green: 0.16, blue: 0.17, alpha: 1).cgColor
            return p
        }
        let header = NSStackView(); header.spacing = 12; root.addArrangedSubview(header); fullWidth(header)
        let appIcon = NSImageView(image: NSImage(named: NSImage.applicationIconName) ?? NSImage())
        appIcon.imageScaling = .scaleProportionallyUpOrDown
        appIcon.widthAnchor.constraint(equalToConstant: 54).isActive = true; appIcon.heightAnchor.constraint(equalToConstant: 54).isActive = true
        header.addArrangedSubview(appIcon)
        let heading = NSStackView(); heading.orientation = .vertical; heading.alignment = .leading; heading.spacing = 3
        let title = NSTextField(labelWithString: "ValeoCapture"); title.font = NSFont.systemFont(ofSize: 25, weight: .semibold)
        heading.addArrangedSubview(title); heading.addArrangedSubview(caption("A new life for your Leaf Valeo 11")); header.addArrangedSubview(heading)
        let spacer = NSView(); spacer.setContentHuggingPriority(.defaultLow, for: .horizontal); header.addArrangedSubview(spacer)
        let refreshButton = button("Refresh", #selector(refresh)); configurationControls.append(refreshButton); header.addArrangedSubview(refreshButton)
        loginButton = button("Check Connection", #selector(probe)); loginButton.isEnabled = false; header.addArrangedSubview(loginButton)

        let main = NSStackView(); main.orientation = .horizontal; main.spacing = 16; main.alignment = .top; root.addArrangedSubview(main); fullWidth(main)
        let settings = panel(); settings.widthAnchor.constraint(equalToConstant: 330).isActive = true
        let settingsTitle = NSTextField(labelWithString: "Capture settings"); settingsTitle.font = NSFont.systemFont(ofSize: 16, weight: .semibold)
        settings.addArrangedSubview(settingsTitle)
        settings.addArrangedSubview(caption("SOFTWARE GAIN"))
        iso.addItems(withTitles: softwareISOValues.map { "\($0 / 25)×  ·  ISO \($0) equivalent" })
        let savedISO = Int(UserDefaults.standard.string(forKey: "captureISO") ?? "25") ?? 25
        iso.selectItem(at: softwareISOValues.firstIndex(of: savedISO) ?? 0)
        iso.toolTip = "Valeo 11's verified Leaf workflow uses software ISO. Digital exposure changes, not analogue sensor gain. Changes during transfer apply to the next image."
        iso.target = self; iso.action = #selector(changeISO); settings.addArrangedSubview(iso)
        settings.addArrangedSubview(caption("ORIENTATION"))
        orientation.addItems(withTitles: ["Landscape", "Portrait clockwise", "Landscape inverted", "Portrait counterclockwise"])
        orientation.selectItem(at: UserDefaults.standard.integer(forKey: "orientationIndex"))
        orientation.target = self; orientation.action = #selector(saveOrientation); settings.addArrangedSubview(orientation)
        configurationControls += [iso, orientation]
        settings.addArrangedSubview(caption("LEAF COLOUR PRESET · TIFF"))
        bundledProfiles = BundledLeafProfile.load()
        if !bundledProfiles.isEmpty {
            leafPresets.addItem(withTitle: "Choose a bundled preset…"); leafPresets.addItems(withTitles: bundledProfiles.map { $0.name })
            leafPresets.target = self; leafPresets.action = #selector(selectBundledProfile)
            settings.addArrangedSubview(leafPresets); configurationControls.append(leafPresets)
        }
        let profileRow = NSStackView(); profileRow.spacing = 8
        let importButton = button("Import ICC…", #selector(importProfile)); configurationControls.append(importButton); profileRow.addArrangedSubview(importButton)
        profile.font = NSFont.systemFont(ofSize: 11); profile.textColor = .secondaryLabelColor
        profile.lineBreakMode = .byTruncatingMiddle; profile.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        profileRow.addArrangedSubview(profile); settings.addArrangedSubview(profileRow)
        settings.addArrangedSubview(caption("SAVE TO"))
        destination.font = NSFont.systemFont(ofSize: 11); destination.textColor = .secondaryLabelColor; destination.lineBreakMode = .byTruncatingMiddle
        settings.addArrangedSubview(destination)
        let folderButton = button("Choose Folder…", #selector(chooseFolder)); configurationControls.append(folderButton); settings.addArrangedSubview(folderButton)
        tiff.state = (UserDefaults.standard.object(forKey: "saveTIFF") as? Bool ?? true) ? .on : .off
        tiff.title = "Also save a 16-bit TIFF"; tiff.target = self; tiff.action = #selector(saveTIFFPreference)
        tiff.toolTip = "DNG always keeps editable raw data. TIFF adds a rendered copy through the selected Leaf colour preset."
        tiff.isEnabled = calibration != nil; configurationControls.append(tiff); settings.addArrangedSubview(tiff)
        let settingsSpace = NSView(); settingsSpace.setContentHuggingPriority(.init(1), for: .vertical)
        settings.addArrangedSubview(settingsSpace)
        for control in [iso, orientation, leafPresets, profileRow, destination] as [NSView] {
            if control.superview != nil { control.widthAnchor.constraint(equalToConstant: 294).isActive = true }
        }
        main.addArrangedSubview(settings)
        settings.heightAnchor.constraint(equalTo: main.heightAnchor).isActive = true

        let imagePanel = panel(); imagePanel.spacing = 10
        main.addArrangedSubview(imagePanel)
        imagePanel.widthAnchor.constraint(equalTo: main.widthAnchor, constant: -346).isActive = true
        imagePanel.heightAnchor.constraint(equalTo: settings.heightAnchor).isActive = true
        let imageHeader = NSStackView(); imageHeader.spacing = 10
        let imageTitle = NSTextField(labelWithString: "Latest capture"); imageTitle.font = NSFont.systemFont(ofSize: 16, weight: .semibold)
        imageHeader.addArrangedSubview(imageTitle)
        revealButton = button("Reveal in Finder", #selector(revealLastCapture)); revealButton.isEnabled = false; imageHeader.addArrangedSubview(revealButton)
        imagePanel.addArrangedSubview(imageHeader)
        let imageArea = NSView(); imageArea.wantsLayer = true; imageArea.layer?.cornerRadius = 9
        imageArea.layer?.backgroundColor = NSColor(calibratedWhite: 0.08, alpha: 1).cgColor
        preview.imageScaling = .scaleProportionallyUpOrDown; preview.setAccessibilityLabel("Last capture preview")
        preview.translatesAutoresizingMaskIntoConstraints = false; imageArea.addSubview(preview)
        previewPlaceholder.textColor = .secondaryLabelColor; previewPlaceholder.alignment = .center; previewPlaceholder.font = NSFont.systemFont(ofSize: 12)
        previewPlaceholder.translatesAutoresizingMaskIntoConstraints = false; imageArea.addSubview(previewPlaceholder)
        NSLayoutConstraint.activate([preview.leadingAnchor.constraint(equalTo: imageArea.leadingAnchor, constant: 8),
            preview.trailingAnchor.constraint(equalTo: imageArea.trailingAnchor, constant: -8), preview.topAnchor.constraint(equalTo: imageArea.topAnchor, constant: 8),
            preview.bottomAnchor.constraint(equalTo: imageArea.bottomAnchor, constant: -8), previewPlaceholder.centerXAnchor.constraint(equalTo: imageArea.centerXAnchor),
            previewPlaceholder.centerYAnchor.constraint(equalTo: imageArea.centerYAnchor), previewPlaceholder.widthAnchor.constraint(equalTo: imageArea.widthAnchor, constant: -40)])
        imagePanel.addArrangedSubview(imageArea)
        imageArea.widthAnchor.constraint(equalTo: imagePanel.widthAnchor, constant: -36).isActive = true
        imageArea.heightAnchor.constraint(greaterThanOrEqualToConstant: 240).isActive = true
        previewName.font = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .regular); previewName.textColor = .secondaryLabelColor
        previewName.lineBreakMode = .byTruncatingMiddle; imagePanel.addArrangedSubview(previewName)
        previewName.widthAnchor.constraint(equalTo: imageArea.widthAnchor).isActive = true

        let capturePanel = panel(); capturePanel.spacing = 7; root.addArrangedSubview(capturePanel); fullWidth(capturePanel)
        capturePanel.heightAnchor.constraint(equalToConstant: 190).isActive = true
        status.font = NSFont.systemFont(ofSize: 17, weight: .semibold); capturePanel.addArrangedSubview(status)
        detail.textColor = .secondaryLabelColor; detail.font = NSFont.systemFont(ofSize: 12)
        detail.preferredMaxLayoutWidth = 830; capturePanel.addArrangedSubview(detail)
        let captureRow = NSStackView(); captureRow.spacing = 10
        captureButton = button("Start Listening", #selector(startCapture)); captureButton.isEnabled = false
        captureButton.bezelColor = NSColor(calibratedRed: 0.43, green: 0.56, blue: 0.41, alpha: 1); captureRow.addArrangedSubview(captureButton)
        stopButton = button("Stop", #selector(stopCapture)); stopButton.isEnabled = false; captureRow.addArrangedSubview(stopButton)
        captureRow.addArrangedSubview(button("Show Captures", #selector(showCaptures)))
        capturePanel.addArrangedSubview(captureRow)
        progress.style = .bar; progress.minValue = 0; progress.maxValue = 1; progress.isIndeterminate = false
        progress.setAccessibilityLabel("Photograph and fresh calibration transfer")
        capturePanel.addArrangedSubview(progress); progress.widthAnchor.constraint(equalTo: capturePanel.widthAnchor, constant: -36).isActive = true
        captureStep.font = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .medium); captureStep.textColor = .secondaryLabelColor; capturePanel.addArrangedSubview(captureStep)
        exportStatus.font = NSFont.systemFont(ofSize: 11); exportStatus.textColor = .secondaryLabelColor; capturePanel.addArrangedSubview(exportStatus)
        lastTiming.font = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .regular); lastTiming.textColor = .secondaryLabelColor; capturePanel.addArrangedSubview(lastTiming)
        let scope = NSTextField(wrappingLabelWithString: "Valeo 11 uses base ISO 25 in the verified workflow; higher ISO is software gain. DNG retains raw headroom. Factory gain/defect matching is still being verified.")
        scope.font = NSFont.systemFont(ofSize: 10); scope.textColor = .secondaryLabelColor; scope.preferredMaxLayoutWidth = 680
        let footer = NSStackView(); footer.spacing = 14; footer.addArrangedSubview(scope)
        scope.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let donate = button("♥ Support on Ko-fi", #selector(supportProject)); donate.bezelStyle = .inline; donate.controlSize = .small
        donate.toolTip = "Support ValeoCapture development at ko-fi.com/oakman"
        footer.addArrangedSubview(donate); root.addArrangedSubview(footer); fullWidth(footer)
        if let output = showcaseOutput { renderShowcase(to: output); return }
        window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
        if let path = UserDefaults.standard.string(forKey: "destination") { destinationURL = URL(fileURLWithPath: path); destination.stringValue = path }
        if let name = UserDefaults.standard.string(forKey: "profileName") { profile.stringValue = name }
        if destinationURL == nil {
            let folder = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Pictures/ValeoCapture", isDirectory: true)
            do { try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true); destinationURL = folder; destination.stringValue = folder.path }
            catch { detail.stringValue = error.localizedDescription }
        }
        if let index = bundledProfiles.firstIndex(where: { $0.url.lastPathComponent == (UserDefaults.standard.string(forKey: "profileName") ?? "LF2 Portrait4.ICC") }) {
            leafPresets.selectItem(at: index + 1)
            if UserDefaults.standard.string(forKey: "profilePath").map({ FileManager.default.fileExists(atPath: $0) }) != true { loadProfile(bundledProfiles[index].url) }
        }
        captureTimer = Timer.scheduledTimer(withTimeInterval: 0.35, repeats: true) { [weak self] _ in self?.pollCapture() }
        refresh()
    }
    private func button(_ title: String, _ action: Selector) -> NSButton { NSButton(title: title, target: self, action: action) }

    // README showcase: the real window in a representative state, rendered offscreen to PNG.
    // No camera, capture service, preferences or folders are touched; the preview is a
    // procedurally generated test chart, never a photograph.
    private var showcaseOutput: URL? {
        guard let index = CommandLine.arguments.firstIndex(of: "--render-showcase"), index + 1 < CommandLine.arguments.count else { return nil }
        return URL(fileURLWithPath: CommandLine.arguments[index + 1])
    }
    private func syntheticChart(width: Int, height: Int) -> NSImage {
        let image = NSImage(size: NSSize(width: width, height: height))
        image.lockFocus()
        let w = CGFloat(width), h = CGFloat(height)
        NSGradient(colors: [NSColor(calibratedWhite: 0.16, alpha: 1), NSColor(calibratedWhite: 0.07, alpha: 1)])!.draw(in: NSRect(x: 0, y: 0, width: w, height: h), angle: -90)
        // 6 x 4 colour patches: evenly spaced hues and saturations, purely arithmetic.
        let cols = 6, rows = 4, margin = w * 0.06, gap = w * 0.012
        let gridW = w * 0.56, cell = (gridW - gap * CGFloat(cols - 1)) / CGFloat(cols)
        let top = h - margin
        for r in 0..<rows { for c in 0..<cols {
            let colour: NSColor = r == rows - 1
                ? NSColor(calibratedWhite: CGFloat(cols - 1 - c) / CGFloat(cols - 1) * 0.92 + 0.04, alpha: 1)
                : NSColor(calibratedHue: CGFloat(c) / CGFloat(cols) + CGFloat(r) * 0.055, saturation: 0.75 - CGFloat(r) * 0.18, brightness: 0.82, alpha: 1)
            colour.setFill()
            NSBezierPath(roundedRect: NSRect(x: margin + CGFloat(c) * (cell + gap), y: top - CGFloat(r + 1) * cell - CGFloat(r) * gap, width: cell, height: cell), xRadius: 4, yRadius: 4).fill()
        } }
        // Smooth grey ramp and RGB ramps below the patches.
        let rampY = top - CGFloat(rows) * (cell + gap) - h * 0.08, rampH = h * 0.05
        for (i, tint) in [NSColor.white, NSColor.systemRed, NSColor.systemGreen, NSColor.systemBlue].enumerated() {
            NSGradient(colors: [NSColor.black, tint])!.draw(in: NSRect(x: margin, y: rampY - CGFloat(i) * (rampH + gap), width: gridW, height: rampH), angle: 0)
        }
        // Siemens star for sharpness.
        let centre = NSPoint(x: margin + gridW + (w - margin - gridW) / 2, y: h * 0.56), radius = min(w * 0.17, h * 0.3)
        NSColor(calibratedWhite: 0.92, alpha: 1).setFill(); NSBezierPath(ovalIn: NSRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2)).fill()
        NSColor(calibratedWhite: 0.08, alpha: 1).setFill()
        let spokes = 36
        for k in stride(from: 0, to: spokes, by: 2) {
            let a0 = CGFloat(k) / CGFloat(spokes) * 360, a1 = CGFloat(k + 1) / CGFloat(spokes) * 360
            let wedge = NSBezierPath(); wedge.move(to: centre)
            wedge.appendArc(withCenter: centre, radius: radius, startAngle: a0, endAngle: a1); wedge.close(); wedge.fill()
        }
        let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedSystemFont(ofSize: h * 0.03, weight: .medium), .foregroundColor: NSColor(calibratedWhite: 0.72, alpha: 1)]
        ("SYNTHETIC TEST CHART" as NSString).draw(at: NSPoint(x: centre.x - radius, y: h * 0.17), withAttributes: attributes)
        ("not a photograph" as NSString).draw(at: NSPoint(x: centre.x - radius, y: h * 0.12), withAttributes: attributes)
        image.unlockFocus()
        return image
    }
    private func renderShowcase(to url: URL) {
        status.stringValue = "Listening · ready for the shutter"
        detail.stringValue = "Leaf Valeo 11 connected. Fire the camera's shutter; each photograph is received with a fresh dark frame."
        destination.stringValue = "~/Pictures/ValeoCapture"; profile.stringValue = "Imported preset: Portrait.icc"
        iso.selectItem(at: min(1, max(0, iso.numberOfItems - 1)))
        captureStep.stringValue = "Ready for the shutter  ·  3 photographs saved this session"
        exportStatus.stringValue = "DNG and 16-bit TIFF saved for the last photograph."
        lastTiming.stringValue = "Last capture 7.9 s  ·  photograph 2.7 s  ·  dark frame 4.3 s  ·  export 0.5 s"
        progress.doubleValue = 1
        preview.image = syntheticChart(width: 1503, height: 1002); previewPlaceholder.isHidden = true
        previewName.stringValue = "Valeo11-0000000000000003.tiff  ·  4008 × 2672  ·  synthetic preview"
        loginButton.isEnabled = true; revealButton.isEnabled = true; captureButton.isEnabled = false; stopButton.isEnabled = true
        guard let view = window.contentView else { exit(1) }
        view.layoutSubtreeIfNeeded(); view.display()
        guard let content = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
        view.cacheDisplay(in: view.bounds, to: content)
        // The window background is not part of the content view: composite onto it, in pixels.
        let width = content.pixelsWide, height = content.pixelsHigh
        guard let source = content.cgImage,
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { exit(1) }
        let full = CGRect(x: 0, y: 0, width: width, height: height)
        context.setFillColor((window.backgroundColor ?? .black).usingColorSpace(.sRGB)!.cgColor); context.fill(full)
        context.draw(source, in: full)
        guard let composed = context.makeImage() else { exit(1) }
        let rep = NSBitmapImageRep(cgImage: composed)
        guard let png = rep.representation(using: .png, properties: [:]) else { exit(1) }
        do { try png.write(to: url, options: .withoutOverwriting) } catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
        print("Rendered showcase \(rep.pixelsWide)x\(rep.pixelsHigh) to \(url.path)"); exit(0)
    }
    private func result(_ json: String) -> [String: Any] {
        guard let data = json.data(using: .utf8), let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return [:] }
        return value["schema"] as? Int == 1 ? value : [:]
    }
    private func alert(_ message: String) {
        let a = NSAlert(); a.messageText = message; a.runModal()
    }
    @objc private func refresh() {
        guard !capturing else { return }; cameraReady = false; updateCaptureControls()
        status.stringValue = "Looking for a Leaf Valeo 11…"; loginButton.isEnabled = false
        client.call({ $0.snapshot($1) }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; let value = self.result(json)
            guard value["ok"] as? Bool == true else {
                self.status.stringValue = "Discovery unavailable"; self.detail.stringValue = value["error"] as? String ?? "Capture service returned an invalid reply"; return
            }
            let devices = value["devices"] as? [[String: Any]] ?? []
            self.registryID = devices.first?["registryID"] as? String
            self.loginButton.isEnabled = devices.count == 1
            self.status.stringValue = devices.isEmpty ? "No Leaf camera found" : "Found \(devices.count) Leaf SBP2 candidate(s)"
            self.detail.stringValue = devices.isEmpty ? "Connect and power the back, then refresh." : "Close other camera apps before connecting."
            if devices.count == 1 && self.calibration != nil { self.probe() }
        }}
    }
    @objc private func probe() {
        guard let id = registryID else { return }; loginButton.isEnabled = false
        client.call({ proxy, reply in
            if self.calibration != nil { proxy.inspect(id, reply: reply) } else { proxy.probe(id, reply: reply) }
        }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; let value = self.result(json)
            self.loginButton.isEnabled = true
            self.detail.stringValue = value["ok"] as? Bool == true ? "Connection completed and closed. Camera identity and image capture still need validation." : (value["error"] as? String ?? "Connection failed")
            self.cameraReady = value["captureReady"] as? Bool == true && value["bodySerial"] as? String == self.calibration?.bodySerial
            if self.cameraReady {
                self.status.stringValue = "Leaf Valeo 11 available"
                self.detail.stringValue = "Select software gain and a Leaf preset, then start listening."
            } else if value["ok"] as? Bool == true && self.calibration != nil {
                self.detail.stringValue = "The capture backend or this back’s calibration is unavailable."
            }
            self.updateCaptureControls()
        }}
    }
    private func updateCaptureControls() {
        for control in configurationControls { control.isEnabled = !capturing && !profileLoading }
        iso.isEnabled = !startingCapture && !stopRequested && !profileLoading; tiff.isEnabled = calibration != nil && !capturing
        loginButton.isEnabled = registryID != nil && !capturing
        captureButton.isEnabled = cameraReady && !capturing && !profileLoading && destinationURL != nil && UserDefaults.standard.string(forKey: "profilePath") != nil
        stopButton.isEnabled = capturing && !startingCapture && !stopRequested
    }
    @objc private func startCapture() {
        guard !profileLoading, let id = registryID, let folder = destinationURL, let calibration = calibration,
              let path = UserDefaults.standard.string(forKey: "profilePath"), let selectedISO = selectedSoftwareISO else { return }
        do {
            let file = try FileHandle(forReadingFrom: URL(fileURLWithPath: path)); defer { file.closeFile() }
            let data = file.readData(ofLength: 16 * 1024 * 1024 + 1)
            guard data.count >= 128 && data.count <= 16 * 1024 * 1024 else { throw NSError(domain: "ValeoCapture", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid profile size"]) }
            let rotation = [1, 6, 3, 8][orientation.indexOfSelectedItem]
            UserDefaults.standard.set(String(selectedISO), forKey: "captureISO")
            capturing = true; startingCapture = true; stopRequested = false; updateCaptureControls(); status.stringValue = "Starting capture…"
            client.call({ $0.startCapture(id, folder: folder.path, iso: selectedISO, orientation: rotation,
                                          profile: data, calibration: calibration.data, saveTIFF: self.tiff.state == .on, reply: $1) }) { [weak self] json in DispatchQueue.main.async {
                guard let self = self else { return }; let value = self.result(json)
                self.startingCapture = false
                if value["ok"] as? Bool != true { self.capturing = false; self.detail.stringValue = value["error"] as? String ?? "Could not start capture" }
                else {
                    self.client.call({ $0.waitForCaptureToFinish($1) }) { [weak self] _ in
                        DispatchQueue.main.async { self?.pollCapture() }
                    }
                }
                self.updateCaptureControls()
                if self.terminating {
                    if self.capturing { self.stopCapture() } else { NSApp.reply(toApplicationShouldTerminate: true) }
                }
            }}
        } catch { alert(error.localizedDescription) }
    }
    @objc private func stopCapture() {
        guard capturing else { return }; stopRequested = true; stopButton.isEnabled = false; status.stringValue = "Stopping after the current capture…"
        guard !startingCapture else { return }
        client.call({ $0.stopCapture($1) }) { _ in }
    }
    @objc private func changeISO() {
        guard let selected = selectedSoftwareISO else { return }
        UserDefaults.standard.set(String(selected), forKey: "captureISO")
        guard capturing else { return }
        client.call({ $0.requestSoftwareISO(selected, reply: $1) }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; let value = self.result(json)
            if value["ok"] as? Bool != true { self.alert(value["error"] as? String ?? "Software gain request failed") }
        }}
    }
    private var selectedSoftwareISO: Int? {
        softwareISOValues.indices.contains(iso.indexOfSelectedItem) ? softwareISOValues[iso.indexOfSelectedItem] : nil
    }
    private func pollCapture() {
        guard capturing && !startingCapture && !stateRequestPending else { return }; stateRequestPending = true
        client.call({ $0.captureState($1) }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; self.stateRequestPending = false; let value = self.result(json)
            guard let active = value["active"] as? Bool else { self.detail.stringValue = value["error"] as? String ?? "Capture service unavailable"; return }
            self.capturing = active; if !active { self.stopRequested = false }; self.updateCaptureControls()
            let phase = active && self.stopRequested ? "Stopping after the current capture" : value["phase"] as? String ?? "Capture"
            let completed = value["completed"] as? Int ?? 0
            let stage = value["stage"] as? String ?? ""
            let exporting = value["exportPending"] as? Bool ?? false
            self.status.stringValue = "\(phase) · \(completed) saved"
            self.progress.doubleValue = value["fraction"] as? Double ?? 0
            let error = value["error"] as? String ?? ""
            let timing = value["timing"] as? String ?? ""
            self.lastTiming.stringValue = timing
            if !active && !error.isEmpty { self.cameraReady = false; self.updateCaptureControls() }
            self.detail.stringValue = error.isEmpty ? (stage == "waiting" ? "Fire the physical shutter. Files can finish saving in the background." : "Wait for Ready before the next exposure. Original sensor data is preserved.") : error
            let received = value["received"] as? Double ?? 0
            switch stage {
            case "photograph": self.captureStep.stringValue = String(format: "1 of 2 · Photograph · %.1f / 21.8 MB", received / 1_000_000)
            case "calibration":
                self.captureStep.stringValue = String(format: "2 of 2 · Fresh calibration · %.1f / 21.8 MB", max(0, received - 21_772_608) / 1_000_000)
            case "waiting": self.captureStep.stringValue = "Back ready · photograph + fresh calibration received for each image"
            case "finishing": self.captureStep.stringValue = "Finishing files safely before continuing…"
            default: self.captureStep.stringValue = active ? "Preparing the back…" : "Listening stopped"
            }
            self.exportStatus.stringValue = exporting ? "Export · \(value["exportPhase"] as? String ?? "Saving files")" : (completed > 0 ? "All completed captures saved · DNG\(self.tiff.state == .on ? " + 16-bit TIFF" : "")" : "DNG is always saved. TIFF uses your selected Leaf preset.")
            if active, let applied = value["softwareISO"] as? Int, self.softwareISOValues.contains(applied), let requested = value["requestedISO"] as? Int {
                self.status.stringValue += " · gain \(applied / 25)× (ISO \(applied))"
                if error.isEmpty && requested != applied { self.detail.stringValue = "Gain \(requested / 25)× (ISO \(requested)) is queued for the next image. The current image keeps its original gain." }
            }
            let preview = (value["lastTIFF"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? value["lastDNG"] as? String ?? ""
            if completed > 0 && !preview.isEmpty && preview != self.lastPreviewPath {
                self.lastPreviewPath = preview
                self.revealButton.isEnabled = true
                self.previewName.stringValue = URL(fileURLWithPath: preview).lastPathComponent
                DispatchQueue.global(qos: .userInitiated).async {
                    guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: preview) as CFURL, nil),
                          let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                            kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: 1000] as CFDictionary) else { return }
                    DispatchQueue.main.async {
                        guard self.lastPreviewPath == preview else { return }
                        self.preview.image = NSImage(cgImage: image, size: .zero); self.previewPlaceholder.isHidden = true
                    }
                }
            }
            if !active && self.terminating { NSApp.reply(toApplicationShouldTerminate: true) }
        }}
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if capturing { terminating = true; stopCapture(); return .terminateLater }; return .terminateNow
    }
    @objc private func showCaptures() { if let folder = destinationURL { NSWorkspace.shared.open(folder) } }
    @objc private func supportProject() { NSWorkspace.shared.open(URL(string: "https://ko-fi.com/oakman")!) }
    @objc private func revealLastCapture() {
        if !lastPreviewPath.isEmpty { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: lastPreviewPath)]) }
    }
    @objc private func saveTIFFPreference() { UserDefaults.standard.set(tiff.state == .on, forKey: "saveTIFF") }
    @objc private func saveOrientation() { UserDefaults.standard.set(orientation.indexOfSelectedItem, forKey: "orientationIndex") }
    @objc private func chooseFolder() {
        let panel = NSOpenPanel(); panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.canCreateDirectories = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        destinationURL = url; destination.stringValue = url.path; UserDefaults.standard.set(url.path, forKey: "destination")
        updateCaptureControls()
    }
    @objc private func importProfile() {
        let panel = NSOpenPanel(); panel.allowedFileTypes = ["icc", "icm"]; panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        loadProfile(url)
    }
    @objc private func selectBundledProfile() {
        let index = leafPresets.indexOfSelectedItem - 1
        guard bundledProfiles.indices.contains(index) else { return }
        loadProfile(bundledProfiles[index].url)
    }
    private func loadProfile(_ url: URL) {
        guard !capturing && !profileLoading else { return }
        let data: Data
        do {
            let file = try FileHandle(forReadingFrom: url); defer { file.closeFile() }
            data = file.readData(ofLength: 16 * 1024 * 1024 + 1)
            guard data.count >= 128, data.count <= 16 * 1024 * 1024 else { alert("ICC profile size outside bounds"); return }
        } catch { alert(error.localizedDescription); return }
        profileLoading = true; updateCaptureControls()
        client.call({ $0.validateProfile(data, reply: $1) }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; let value = self.result(json)
            self.profileLoading = false
            defer { self.updateCaptureControls() }
            guard value["ok"] as? Bool == true else {
                self.leafPresets.selectItem(at: 0)
                self.alert(value["error"] as? String ?? "Invalid RGB profile"); return
            }
            do {
                let base = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
                    .appendingPathComponent("ValeoCapture/Profiles", isDirectory: true)
                try FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
                let copy = base.appendingPathComponent(UUID().uuidString).appendingPathExtension("icc")
                // Store exactly the owned bytes validated over XPC, even if the
                // selected source file changes before validation completes.
                try data.write(to: copy, options: .withoutOverwriting)
                UserDefaults.standard.set(copy.path, forKey: "profilePath")
                UserDefaults.standard.set(url.lastPathComponent, forKey: "profileName")
                self.profile.stringValue = url.lastPathComponent
                self.detail.stringValue = "Leaf preset selected for rendered TIFF output. DNG retains editable raw colour metadata."
                self.updateCaptureControls()
            } catch { self.alert(error.localizedDescription) }
        }}
    }
    @objc private func exportSynthetic() {
        let panel = NSSavePanel(); panel.nameFieldStringValue = "ValeoCapture-Synthetic.dng"; panel.allowedFileTypes = ["dng"]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        client.call({ $0.syntheticDNG(url.path, reply: $1) }) { [weak self] json in DispatchQueue.main.async {
            guard let self = self else { return }; let value = self.result(json)
            if value["ok"] as? Bool == true {
                self.detail.stringValue = "Exported a synthetic 64 × 64 DNG fixture."
                self.preview.image = NSImage(contentsOf: url)
            } else { self.alert(value["error"] as? String ?? "Export failed") }
        }}
    }
    @objc private func unavailable() {}
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

if CommandLine.arguments.contains("--self-test-bundled-profiles") {
    let client = CaptureClient(), profiles = BundledLeafProfile.load()
    var finished = false, valid = true, checked = 0
    func next() {
        guard checked < profiles.count else { finished = true; return }
        let profile = profiles[checked]
        guard let file = try? FileHandle(forReadingFrom: profile.url) else { valid = false; finished = true; return }
        let data = file.readData(ofLength: 16 * 1024 * 1024 + 1);file.closeFile()
        client.call({ $0.validateProfile(data, reply: $1) }) { json in DispatchQueue.main.async {
            guard let bytes = json.data(using: .utf8), let result = (try? JSONSerialization.jsonObject(with: bytes)) as? [String: Any], result["ok"] as? Bool == true else { valid = false;finished = true;return }
            checked += 1;next()
        }}
    }
    next();let limit = Date().addingTimeInterval(20)
    while !finished && Date() < limit { RunLoop.current.run(until: Date().addingTimeInterval(0.05)) }
    let ok = valid && finished && !profiles.isEmpty
    print("{\"ok\":\(ok),\"bundledProfilesValidated\":\(checked)}")
    exit(ok ? 0 : 1)
} else if CommandLine.arguments.contains("--self-test-xpc") {
    let client = CaptureClient(); var finished = false; var ok = false
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("valeo-xpc-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
    let output = directory.appendingPathComponent("synthetic.dng")
    func finish(_ json: String, _ success: Bool) {
        DispatchQueue.main.async { print(json); ok = success; finished = true }
    }
    client.call({ $0.snapshot($1) }) { json in
        guard json.contains("\"schema\":1"), json.contains("\"ok\":true"), json.contains("\"captureReady\":false") else { finish(json, false); return }
        client.call({ $0.validateProfile(Data([1, 2, 3]), reply: $1) }) { invalid in
            guard invalid.contains("\"ok\":false"), let profileData = NSColorSpace.sRGB.iccProfileData else { finish(invalid, false); return }
            client.call({ $0.validateProfile(profileData, reply: $1) }) { profile in
                guard profile.contains("\"ok\":true") else { finish(profile, false); return }
                client.call({ $0.syntheticDNG(output.path, reply: $1) }) { image in
                    guard image.contains("\"synthetic\":true"), let data = try? Data(contentsOf: output), data.count > 8192,
                          data.prefix(4) == Data([0x49, 0x49, 42, 0]) else { finish(image, false); return }
                    client.call({ $0.syntheticDNG(output.path, reply: $1) }) { duplicate in
                        guard duplicate.contains("\"ok\":false"), (try? Data(contentsOf: output)) == data else { finish(duplicate, false); return }
                        finish("{\"ok\":true,\"checks\":[\"discovery\",\"invalid ICC\",\"RGB ICC\",\"synthetic DNG\",\"no overwrite\"]}", true)
                    }
                }
            }
        }
    }
    let limit = Date().addingTimeInterval(15)
    while !finished && Date() < limit { RunLoop.current.run(until: Date().addingTimeInterval(0.05)) }
    if !finished { fputs("XPC snapshot timed out\n", stderr) }
    try? FileManager.default.removeItem(at: directory)
    exit(ok ? 0 : 1)
} else {
    let app = NSApplication.shared
    let delegate = AppDelegate(); app.delegate = delegate; app.run()
}
