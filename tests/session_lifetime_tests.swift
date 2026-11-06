// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation

@main struct SessionLifetimeTests {
    static func main() {
        let a = SessionLifetime(), b = SessionLifetime()
        var first = 0, second = 0
        a.wait { result in precondition(result == "done-a"); first += 1 }
        b.wait { result in precondition(result == "done-b"); second += 1 }
        precondition(first == 0 && second == 0)
        a.finish("done-a"); precondition(first == 1 && second == 0)
        a.finish("wrong"); precondition(first == 1)
        a.wait { result in precondition(result == "done-a"); first += 1 }
        precondition(first == 2)
        b.finish("done-b"); precondition(second == 1)
        let bounded = SessionLifetime(); var overflow = false, replies = 0
        for _ in 0..<8 { bounded.wait { _ in replies += 1 } }
        bounded.wait { overflow = $0.contains("Too many") }
        precondition(overflow && replies == 0); bounded.finish("done"); precondition(replies == 8)
        // Exercise registration versus completion races without hardware/XPC.
        for _ in 0..<100 {
            let latch = SessionLifetime(), called = DispatchSemaphore(value: 0)
            DispatchQueue.global().async { latch.wait { _ in called.signal() } }
            DispatchQueue.global().async { latch.finish("done") }
            precondition(called.wait(timeout: .now() + 2) == .success)
        }
        print("PASS session replies: held until completion, late/repeated finish, bounded waiters, independent sessions, racing registration")
    }
}
