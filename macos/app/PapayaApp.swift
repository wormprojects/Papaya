// Papaya for macOS (native) - app entry. One window, one view (PapayaView)
// that shows the 3DS's top or bottom screen (the two "panels"); [ and ]
// switch. The screens are drawn by the app's own C code.
//
// This branch is never merged into the 3DS version.

import SwiftUI
import AppKit

@main
struct PapayaApp: App {
    init() {
        // Flush settings/theme and stop the playback thread on quit.
        NotificationCenter.default.addObserver(
            forName: NSApplication.willTerminateNotification, object: nil,
            queue: nil) { _ in appShutdown() }
    }

    var body: some Scene {
        // Window, not WindowGroup: one window, no new one per opened file.
        Window("Papaya", id: "main") {
            PapayaViewRepresentable()
                .frame(minWidth: 400, minHeight: 240)   // resizable above this
        }
        .defaultSize(width: 800, height: 480)           // 2x the top screen
    }
}

struct PapayaViewRepresentable: NSViewRepresentable {
    func makeNSView(context: Context) -> PapayaView { PapayaView() }
    func updateNSView(_ v: PapayaView, context: Context) {}
}
