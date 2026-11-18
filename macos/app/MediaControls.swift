// Papaya for macOS (native) - the system's media controls: media keys
// (play/pause, next, previous), Control Center / lock screen / headphone
// buttons, the Now Playing widget with title, artist, cover and a scrubbable
// position. Every command goes through the same player functions the keys do
// (appDoCommand in engine/app.c); all of it runs on the main thread, like the
// frame loop.

import AppKit
import MediaPlayer

final class MediaControls {
    private var lastSongId: UInt32 = 0
    private var lastUpdate = Date.distantPast
    private var artwork: MPMediaItemArtwork?
    private var lastPaused = false

    init() {
        let cc = MPRemoteCommandCenter.shared()

        func on(_ c: MPRemoteCommand, _ f: @escaping () -> Void) {
            c.isEnabled = true
            c.addTarget { _ in DispatchQueue.main.async(execute: f); return .success }
        }
        on(cc.playCommand) { appDoCommand(APP_PLAY) }
        on(cc.pauseCommand) { appDoCommand(APP_PAUSE) }
        on(cc.togglePlayPauseCommand) { appDoCommand(APP_PLAY_PAUSE) }
        on(cc.nextTrackCommand) { appDoCommand(APP_NEXT) }
        on(cc.previousTrackCommand) { appDoCommand(APP_PREVIOUS) }

        // Skip buttons (some keyboards/headsets): by the app's seek step.
        for c in [cc.skipForwardCommand, cc.skipBackwardCommand] {
            c.preferredIntervals = [5]
        }
        cc.skipForwardCommand.isEnabled = true
        cc.skipBackwardCommand.isEnabled = true
        cc.skipForwardCommand.addTarget { _ in
            DispatchQueue.main.async { appDoCommand(APP_SEEK_FORWARD) }
            return .success
        }
        cc.skipBackwardCommand.addTarget { _ in
            DispatchQueue.main.async { appDoCommand(APP_SEEK_BACK) }
            return .success
        }

        // Dragging the position in the Now Playing widget.
        cc.changePlaybackPositionCommand.isEnabled = true
        cc.changePlaybackPositionCommand.addTarget { e in
            guard let e = e as? MPChangePlaybackPositionCommandEvent
            else { return .commandFailed }
            let t = e.positionTime
            DispatchQueue.main.async { appSeekTo(t) }
            return .success
        }
    }

    private func string<T>(_ field: T) -> String {
        withUnsafeBytes(of: field) { raw in
            String(cString: raw.bindMemory(to: CChar.self).baseAddress!)
        }
    }

    /// Call every frame; it refreshes the widget a few times a second.
    func update() {
        let now = Date()
        guard now.timeIntervalSince(lastUpdate) >= 0.25 else { return }
        lastUpdate = now

        var st = appState()
        appGetState(&st)
        let center = MPNowPlayingInfoCenter.default()
        guard st.playing else {
            center.nowPlayingInfo = nil
            center.playbackState = .stopped
            return
        }

        if st.songId != lastSongId {            // a new song: load its cover
            lastSongId = st.songId
            artwork = nil
            var data: UnsafeMutablePointer<UInt8>?
            var len = 0
            let path = string(st.path)
            if appCoverArt(path, &data, &len), let d = data {
                let img = NSImage(data: Data(bytes: d, count: len))
                free(d)
                if let img = img {
                    artwork = MPMediaItemArtwork(boundsSize: img.size) { _ in img }
                }
            }
        }

        var info: [String: Any] = [
            MPMediaItemPropertyTitle: string(st.title),
            MPNowPlayingInfoPropertyElapsedPlaybackTime: st.elapsed,
            MPNowPlayingInfoPropertyPlaybackRate: st.paused ? 0.0 : st.speed,
            MPNowPlayingInfoPropertyDefaultPlaybackRate: 1.0,
        ]
        let artist = string(st.artist), album = string(st.album)
        if !artist.isEmpty { info[MPMediaItemPropertyArtist] = artist }
        if !album.isEmpty { info[MPMediaItemPropertyAlbumTitle] = album }
        if st.total > 0 { info[MPMediaItemPropertyPlaybackDuration] = st.total }
        if let a = artwork { info[MPMediaItemPropertyArtwork] = a }
        center.nowPlayingInfo = info
        center.playbackState = st.paused ? .paused : .playing
    }
}
