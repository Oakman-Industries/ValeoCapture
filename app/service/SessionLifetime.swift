// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation

// One latch per session. Finishing an old session must never release a newer
// session's XPC replies. Callbacks run outside the lock, including late waiters.
final class SessionLifetime {
    private let lock = NSLock()
    private var result: String?
    private var replies: [(String) -> Void] = []
    func wait(_ reply: @escaping (String) -> Void) {
        lock.lock()
        if let result = result { lock.unlock(); reply(result); return }
        guard replies.count < 8 else {
            lock.unlock(); reply("{\"schema\":1,\"ok\":false,\"error\":\"Too many session completion requests\"}"); return
        }
        replies.append(reply); lock.unlock()
    }
    func finish(_ result: String) {
        lock.lock()
        guard self.result == nil else { lock.unlock(); return }
        self.result = result; let pending = replies; replies.removeAll(); lock.unlock()
        for reply in pending { reply(result) }
    }
}
