// Papaya for macOS (native) - the view: draws a screen, takes input.
//
// Each display frame (vsync): turn the held keys / mouse into the 3DS button
// bits the app's pages expect, run one appTick() (the app's own frame loop),
// and show the active panel's screen if its pixels changed. The C side
// already redraws only what changed (like on the 3DS); an unchanged screen
// costs one memcmp and no image.

import SwiftUI
import AppKit

enum Panel { case top, bottom
    var other: Panel { self == .top ? .bottom : .top }
}

final class PapayaView: NSView {
    private var panel: Panel = .top
    private var buf: [UInt32] = []
    /// The screen's own layer: exactly `size * zoom` device pixels, top left
    /// (top panel) or centred (bottom panel); the rest of the window is
    /// black. Keeping whole device pixels per screen pixel keeps it crisp.
    private let screenLayer = CALayer()
    /// The menu and the pages (queue, search, value entry, EQ) are an overlay
    /// on whichever panel is up: a dimmed backdrop plus their own 320x240
    /// surface, centred at the biggest whole zoom that fits.
    private let backdropLayer = CALayer()
    private let overlayLayer = CALayer()
    private var overlayBuf = [UInt32](repeating: 0, count: Int(MAC_BOT_W) * Int(MAC_SCR_H))
    private var overlayShown = false
    /// Device pixels per screen pixel the user asked for (Cmd +/-).
    private var zoom = 3
    private var screenSize = (w: Int(MAC_TOP_W), h: Int(MAC_SCR_H))   // top screen, in screen pixels
    private var bottomSize = (w: Int(MAC_BOT_W), h: Int(MAC_SCR_H))   // bottom screen (whole characters)
    private var link: CADisplayLink?
    private let media = MediaControls()

    // Input state
    private var keyButtons: [UInt16: UInt32] = [:]   // keyCode -> button bit
    private var pulses: UInt32 = 0
    private var touching = false
    private var touchFrames = 0                      // scripted taps
    private var touch: (x: Int, y: Int) = (0, 0)
    private var wheelAccum: CGFloat = 0

    // Test script (PAPAYA_SCRIPT="30:A,60:panel,90:snap=x,120:quit")
    private var frameNo = 0
    private var script: [(frame: Int, action: String)] = []

    override var acceptsFirstResponder: Bool { true }

    init() {
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        screenLayer.magnificationFilter = .nearest   // crisp pixels when scaled up
        screenLayer.minificationFilter = .nearest
        screenLayer.contentsGravity = .resize
        screenLayer.actions = ["contents": NSNull(), "bounds": NSNull(),
                               "position": NSNull()]   // no implicit fades
        layer?.addSublayer(screenLayer)
        backdropLayer.backgroundColor = NSColor.black.withAlphaComponent(0.6).cgColor
        backdropLayer.isHidden = true
        layer?.addSublayer(backdropLayer)
        overlayLayer.magnificationFilter = .nearest
        overlayLayer.minificationFilter = .nearest
        overlayLayer.contentsGravity = .resize
        overlayLayer.actions = ["contents": NSNull(), "bounds": NSNull(),
                                "position": NSNull(), "hidden": NSNull()]
        overlayLayer.isHidden = true
        layer?.addSublayer(overlayLayer)
        if let s = ProcessInfo.processInfo.environment["PAPAYA_SCRIPT"] {
            for item in s.split(separator: ",") {
                let p = item.split(separator: ":", maxSplits: 1)
                if p.count == 2, let f = Int(p[0]) { script.append((f, String(p[1]))) }
            }
        }
        appInit()
        resizeBuffer()
    }
    required init?(coder: NSCoder) { fatalError() }
    deinit { link?.invalidate() }

    /// Size of the screen the active panel shows, in screen pixels.
    private var panelSize: (w: Int, h: Int) {
        panel == .top ? screenSize : bottomSize
    }

    private func resizeBuffer() {
        buf = [UInt32](repeating: 0, count: panelSize.w * panelSize.h)
        macInvalidate()
        layoutScreen()
    }

    /// The window changed size (or the zoom did): the TOP screen is laid out
    /// again for the new size (more room = more room, not bigger things), the
    /// bottom one keeps its size for now and is shown at the biggest whole
    /// zoom that fits.
    private func layoutScreen() {
        let scale = window?.backingScaleFactor ?? 2
        let bw = bounds.width * scale, bh = bounds.height * scale
        guard bw >= 1, bh >= 1 else { return }

        // top screen: zoom, then as many screen pixels as fit
        let fit = max(1, Int(min(bw / 400, bh / 240)))
        let z = max(1, min(zoom, fit))
        let tw = min(Int(MAC_MAX_W), max(400, Int(bw) / z))
        let th = min(Int(MAC_MAX_H), max(240, Int(bh) / z))
        if tw != screenSize.w || th != screenSize.h {
            screenSize = (tw, th)
            appResizeTop(Int32(tw), Int32(th))
            if panel == .top {
                buf = [UInt32](repeating: 0, count: tw * th)
                macInvalidate()
            }
        }

        // bottom screen: a text console, so whole 8-pixel characters
        let bc = max(40, min(Int(MAC_MAX_W) / 8, tw / 8))
        let br = max(30, min(Int(MAC_MAX_H) / 8, th / 8))
        if bc * 8 != bottomSize.w || br * 8 != bottomSize.h {
            bottomSize = (bc * 8, br * 8)
            appResizeBottom(Int32(bc), Int32(br))
            if panel == .bottom {
                buf = [UInt32](repeating: 0, count: bc * 8 * br * 8)
                macInvalidate()
            }
        }

        let (cw, ch) = panelSize
        let zz = CGFloat(z)
        let wPts = CGFloat(cw) * zz / scale, hPts = CGFloat(ch) * zz / scale
        let x: CGFloat = 0
        let y = bounds.height - hPts
        screenLayer.contentsScale = scale
        screenLayer.frame = CGRect(x: x.rounded(), y: y.rounded(),
                                   width: wPts, height: hPts)

        // overlay: whole zoom, centred
        let oz = max(1, min(z, Int(min(bw / CGFloat(MAC_BOT_W), bh / CGFloat(MAC_SCR_H)))))
        let ow = CGFloat(MAC_BOT_W) * CGFloat(oz) / scale
        let oh = CGFloat(MAC_SCR_H) * CGFloat(oz) / scale
        backdropLayer.frame = bounds
        overlayLayer.contentsScale = scale
        overlayLayer.frame = CGRect(x: ((bounds.width - ow) / 2).rounded(),
                                    y: ((bounds.height - oh) / 2).rounded(),
                                    width: ow, height: oh)
    }

    override func layout() {
        super.layout()
        layoutScreen()
    }

    // MARK: frame loop (vsync)

    override func viewDidMoveToWindow() {
        link?.invalidate(); link = nil
        guard window != nil else { return }
        window?.makeFirstResponder(self)
        macInvalidate()
        link = displayLink(target: self, selector: #selector(onFrame))
        link?.add(to: .main, forMode: .common)   // keeps going while resizing
    }

    @objc private func onFrame() {
        frameNo += 1
        runScript()
        if touchFrames > 0 { touchFrames -= 1; if touchFrames == 0 { touching = false } }

        macSetInput(keyButtons.values.reduce(0, |), pulses, touching,
                    Int32(touch.x), Int32(touch.y))
        pulses = 0
        appTick()
        media.update()
        if videoQuitRequested() { NSApp.terminate(nil); return }

        // The overlay (menu / pages) over whichever panel is up
        let ov = macOverlayActive()
        if ov != overlayShown {
            overlayShown = ov
            backdropLayer.isHidden = !ov
            overlayLayer.isHidden = !ov
            keyButtons.removeAll(); touching = false
            macInvalidate()
        }
        if ov {
            var ch = false
            overlayBuf.withUnsafeMutableBufferPointer { p in
                ch = macRenderOverlay(p.baseAddress)
            }
            if ch {
                let w = Int(MAC_BOT_W), h = Int(MAC_SCR_H)
                if let prov = CGDataProvider(data: Data(bytes: overlayBuf, count: w * h * 4) as CFData) {
                    overlayLayer.contents = CGImage(
                        width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
                        bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue
                                                 | CGBitmapInfo.byteOrder32Little.rawValue),
                        provider: prov, decode: nil, shouldInterpolate: false,
                        intent: .defaultIntent)
                }
            }
        }

        var changed = false
        buf.withUnsafeMutableBufferPointer { p in
            changed = panel == .top ? macRenderTop(p.baseAddress)
                                    : macRenderBottom(p.baseAddress)
        }
        guard changed else { return }
        let (w, h) = panelSize
        let data = Data(bytes: buf, count: buf.count * 4) as CFData
        guard let prov = CGDataProvider(data: data) else { return }
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue
                                | CGBitmapInfo.byteOrder32Little.rawValue)
        screenLayer.contents = CGImage(width: w, height: h, bitsPerComponent: 8,
                                  bitsPerPixel: 32, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: info, provider: prov,
                                  decode: nil, shouldInterpolate: false,
                                  intent: .defaultIntent)
    }

    func switchPanel() {
        panel = panel.other
        keyButtons.removeAll()      // nothing stays "held" across panels
        touching = false
        resizeBuffer()
    }

    // MARK: keyboard -> 3DS buttons

    /// Button held by a key. What the arrows do depends on what is up, so
    /// they never fight the file browser or the menu:
    ///   top panel:           Left/Right seek, Up/Down previous/next song
    ///   bottom panel or an overlay (menu, pages): the D-pad
    /// Cmd+arrows are the playback controls anywhere.
    /// Return = A, Backspace = B; Q, T and Delete = Y (queue / type a value /
    /// remove: the "other" action); S = shuffle; Space = pause.
    private func button(_ e: NSEvent) -> UInt32? {
        let dpad = panel == .bottom || macOverlayActive()
        switch e.keyCode {
        case 36, 76: return MAC_KEY_A                       // Return
        case 51: return MAC_KEY_B                           // Backspace
        case 49: return MAC_KEY_X                           // Space = pause
        case 1: return MAC_KEY_START                        // S = shuffle
        case 12, 17, 117: return MAC_KEY_Y                  // Q / T / Delete
        case 123: return dpad ? MAC_KEY_LEFT : MAC_KEY_ZL
        case 124: return dpad ? MAC_KEY_RIGHT : MAC_KEY_ZR
        case 126: return dpad ? MAC_KEY_UP : nil
        case 125: return dpad ? MAC_KEY_DOWN : nil
        default: return nil
        }
    }

    override func keyDown(with e: NSEvent) {
        // Cmd+arrows: playback controls from anywhere; Cmd +/- zoom.
        if e.modifierFlags.contains(.command) {
            switch e.keyCode {
            case 123: appDoCommand(APP_SEEK_BACK)
            case 124: appDoCommand(APP_SEEK_FORWARD)
            case 126: if !e.isARepeat { appDoCommand(APP_PREVIOUS) }
            case 125: if !e.isARepeat { appDoCommand(APP_NEXT) }
            case 24, 69: zoom = min(8, zoom + 1); layoutScreen()
            case 27, 78: zoom = max(1, zoom - 1); layoutScreen()
            default: super.keyDown(with: e)
            }
            return
        }

        // A text field (search, typing a value) has the keyboard: characters
        // go to it, Esc cancels / closes it.
        if macTextEntryActive() {
            if e.keyCode == 53 { if !e.isARepeat { pulses |= macEscKey() }; return }
            if e.keyCode == 36 || e.keyCode == 76 { macTypeChar(10); return }   // Return
            if e.keyCode == 51 { macTypeChar(8); return }                       // Backspace
            for u in (e.characters ?? "").unicodeScalars where u.value >= 32 && u.value < 127 {
                macTypeChar(Int32(u.value))
            }
            return
        }

        // Esc: opens the menu; inside the menu and its pages it goes back one
        // level, until you're out (what Esc means is set by the app).
        if e.keyCode == 53 { if !e.isARepeat { pulses |= macEscKey() }; return }

        if e.keyCode == 33 || e.keyCode == 30 {             // [ and ]
            if !e.isARepeat { switchPanel() }
            return
        }
        // Top panel, no overlay: Up/Down are previous/next song (a tap of L / R).
        if panel == .top, !macOverlayActive(), !e.isARepeat {
            if e.keyCode == 126 { pulses |= MAC_KEY_L; return }
            if e.keyCode == 125 { pulses |= MAC_KEY_R; return }
        }
        if let b = button(e) {
            keyButtons[e.keyCode] = b
            pulses |= b     // a tap shorter than a frame still registers
        }
    }

    override func keyUp(with e: NSEvent) { keyButtons[e.keyCode] = nil }

    // MARK: mouse -> stylus

    /// Points per screen pixel of the screen being shown (the overlay's own
    /// when it is up).
    private func canvasScale() -> CGFloat {
        if macOverlayActive() { return overlayLayer.frame.width / CGFloat(MAC_BOT_W) }
        return screenLayer.frame.width / CGFloat(panelSize.w)
    }

    /// A mouse position in the pixels of what is under it: the overlay while
    /// it is up (nil outside it), else the bottom panel (nil on the top one).
    private func canvasPoint(_ e: NSEvent) -> (x: Int, y: Int)? {
        let p = convert(e.locationInWindow, from: nil)
        let sc = canvasScale()
        guard sc > 0 else { return nil }
        if macOverlayActive() {
            let f = overlayLayer.frame
            guard f.contains(p) else { return nil }
            return (Int((p.x - f.minX) / sc), Int((f.maxY - p.y) / sc))
        }
        guard panel == .bottom else { return nil }
        let f = screenLayer.frame
        return (Int((p.x - f.minX) / sc), Int((f.maxY - p.y) / sc))
    }

    override func mouseDown(with e: NSEvent) {
        window?.makeFirstResponder(self)
        if macOverlayActive(), canvasPoint(e) == nil {      // click outside = Esc
            pulses |= macEscKey(); return
        }
        guard let c = canvasPoint(e) else { return }
        touching = true; touch = c
    }
    override func mouseDragged(with e: NSEvent) {
        if touching, let c = canvasPoint(e) { touch = c }
    }
    override func mouseUp(with e: NSEvent) { touching = false }

    /// Real scrolling: the list follows the trackpad by exact text rows (a row
    /// is 8 screen pixels), a mouse wheel notch is 3 rows. Screens with
    /// nothing to scroll (the menu) get D-pad taps instead.
    override func scrollWheel(with e: NSEvent) {
        guard panel == .bottom || macOverlayActive() else { return }
        let rowPts = 8 * canvasScale()
        wheelAccum += e.hasPreciseScrollingDeltas ? -e.scrollingDeltaY / rowPts
                                                  : -e.scrollingDeltaY * 3
        let rows = Int(wheelAccum)          // whole rows, toward zero
        guard rows != 0 else { return }
        wheelAccum -= CGFloat(rows)
        scrollRows(rows)
    }

    private func scrollRows(_ rows: Int) {
        if appScroll(Int32(rows)) { return }
        for _ in 0..<min(abs(rows), 4) {    // the menu: one item per tap
            pulses |= rows > 0 ? MAC_KEY_DOWN : MAC_KEY_UP
        }
    }

    // MARK: test script

    private func runScript() {
        for (f, a) in script where f == frameNo {
            switch a {
            case "A": pulses |= MAC_KEY_A
            case "B": pulses |= MAC_KEY_B
            case "SELECT": pulses |= MAC_KEY_SELECT
            case "START": pulses |= MAC_KEY_START
            case "X": pulses |= MAC_KEY_X
            case "Y", "T": pulses |= MAC_KEY_Y
            case "ESC": pulses |= macEscKey()
            case "UP": pulses |= MAC_KEY_UP
            case "DOWN": pulses |= MAC_KEY_DOWN
            case "LEFT": pulses |= MAC_KEY_LEFT
            case "RIGHT": pulses |= MAC_KEY_RIGHT
            case "panel": switchPanel()
            case "quit": NSApp.terminate(nil)
            case "zoom+": zoom = min(8, zoom + 1); layoutScreen()
            case "zoom-": zoom = max(1, zoom - 1); layoutScreen()
            case "cmd=next": appDoCommand(APP_NEXT)
            case "cmd=prev": appDoCommand(APP_PREVIOUS)
            case "cmd=pause": appDoCommand(APP_PAUSE)
            case "cmd=play": appDoCommand(APP_PLAY)
            case "cmd=fwd": appDoCommand(APP_SEEK_FORWARD)
            default:
                if a.hasPrefix("type=") {
                    for u in a.dropFirst(5).unicodeScalars { macTypeChar(Int32(u.value)) }
                    if a.hasSuffix("+") { macTypeChar(10) }
                } else if a.hasPrefix("size="), let wh = Optional(a.dropFirst(5).split(separator: "x")),
                   wh.count == 2, let w = Double(wh[0]), let h = Double(wh[1]) {
                    window?.setContentSize(NSSize(width: w, height: h))
                } else if a.hasPrefix("scroll="), let n = Int(a.dropFirst(7)) { scrollRows(n) }
                else if a.hasPrefix("tap="), let xy = Optional(a.dropFirst(4).split(separator: "/")),
                        xy.count == 2, let x = Int(xy[0]), let y = Int(xy[1]) {
                    touch = (x, y); touching = true; touchFrames = 3
                } else if a.hasPrefix("snap=") { snapshot(String(a.dropFirst(5))) }
            }
        }
    }

    private func snapshot(_ name: String) {
        guard let dir = ProcessInfo.processInfo.environment["PAPAYA_SNAP"],
              let root = layer else { return }
        let scale = window?.backingScaleFactor ?? 2
        let w = Int(bounds.width * scale), h = Int(bounds.height * scale)
        guard w > 0, h > 0,
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8,
                                  bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue)
        else { return }
        ctx.scaleBy(x: scale, y: scale)
        root.render(in: ctx)
        if let img = ctx.makeImage(),
           let png = NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:]) {
            try? png.write(to: URL(fileURLWithPath: "\(dir)/\(name).png"))
        }
    }
}
