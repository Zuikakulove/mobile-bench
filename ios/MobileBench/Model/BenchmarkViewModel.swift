import Foundation
import UIKit

@MainActor
final class BenchmarkViewModel: ObservableObject {
    struct RunningInfo {
        var fraction: Double = 0
        var testName: String = ""
        var testIndex: Int = 0
        var testCount: Int = 0
        var warmup: Bool = true
    }

    enum State {
        case idle
        case running(RunningInfo)
        case done(BenchmarkResult, rawJson: String)
        case failed(String, rawJson: String?)
    }

    @Published private(set) var state: State = .idle
    private var bridge: MBBenchmarkBridge?

    var isRunning: Bool {
        if case .running = state { return true }
        return false
    }

    func start() {
        guard !isRunning else { return }
        let bridge = MBBenchmarkBridge(deviceInfo: Self.deviceInfo(), scratchDir: NSTemporaryDirectory())
        self.bridge = bridge
        state = .running(RunningInfo())
        // 計測中は画面を消さない
        UIApplication.shared.isIdleTimerDisabled = true

        // 計測は UI (メイン) スレッドをブロックしないよう専用スレッドで実行する。
        // QoS は core のワーカースレッドと同じ userInitiated。
        let thread = Thread { [weak self] in
            let json = bridge.run { step, total, testIndex, testCount, testName, warmup in
                let info = RunningInfo(
                    fraction: total > 0 ? Double(step) / Double(total) : 0,
                    testName: testName,
                    testIndex: testIndex,
                    testCount: testCount,
                    warmup: warmup
                )
                Task { @MainActor in
                    guard let self, self.isRunning else { return }
                    self.state = .running(info)
                }
            }
            Task { @MainActor in
                self?.finish(json)
            }
        }
        thread.qualityOfService = .userInitiated
        thread.name = "MobileBench.runner"
        thread.start()
    }

    func cancel() {
        bridge?.cancel()
    }

    private func finish(_ json: String) {
        bridge = nil
        UIApplication.shared.isIdleTimerDisabled = false
        do {
            state = .done(try BenchmarkResult.parse(json), rawJson: json)
        } catch {
            state = .failed(error.localizedDescription, rawJson: json)
        }
    }

    private static func deviceInfo() -> [String: String] {
        var systemInfo = utsname()
        uname(&systemInfo)
        let machine = withUnsafeBytes(of: &systemInfo.machine) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        let device = UIDevice.current
        return [
            "platform": "ios",
            "manufacturer": "Apple",
            "model": machine,  // 例: "iPhone15,2" (シミュレータでは "arm64" / "x86_64")
            "model_name": device.model,
            "os_version": device.systemVersion,
            "app_version": Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "",
        ]
    }
}
