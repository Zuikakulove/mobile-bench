import Combine
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
        var phase: String = ""
        var waitMs: Int = -1
        var attempt: Int = 1
    }

    enum State {
        case idle
        case running(RunningInfo)
        case done(BenchmarkResult, rawJson: String)
        case failed(String, rawJson: String?)
    }

    @Published private(set) var state: State = .idle
    /// 診断モード (行列サイズ掃引など)。スコアには影響しない
    @Published var diagnostics = false
    private var bridge: MBBenchmarkBridge?
    private var backgroundObserver: AnyCancellable?

    init() {
        // 計測中にアプリが前面を離れたら、計測を中止して無効にする (docs/PHASE1_PLAN.md P3.10、J6)。
        // UIApplication.didEnterBackgroundNotification は、アプリがバックグラウンドに入ったときに送られる
        backgroundObserver = NotificationCenter.default
            .publisher(for: UIApplication.didEnterBackgroundNotification)
            .sink { [weak self] _ in
                guard let self else { return }
                Task { @MainActor in self.handleLeftForeground() }
            }
    }

    var isRunning: Bool {
        if case .running = state { return true }
        return false
    }

    func start() {
        guard !isRunning else { return }
        let bridge = MBBenchmarkBridge(
            deviceInfo: Self.deviceInfo(),
            scratchDir: NSTemporaryDirectory(),
            diagnostics: diagnostics
        )
        bridge.conditionProvider = Self.makeConditionProvider()
        self.bridge = bridge
        state = .running(RunningInfo())
        // 計測中は画面を消さない
        UIApplication.shared.isIdleTimerDisabled = true

        // 計測は UI (メイン) スレッドをブロックしないよう専用スレッドで実行する。
        // QoS は core のワーカースレッドと同じ userInitiated。
        // (ViewModel はアプリと同じ寿命なので self は強参照でよい)
        let thread = Thread {
            let json = bridge.run { step, total, testIndex, testCount, testName, warmup, phase, waitMs, attempt in
                let info = RunningInfo(
                    fraction: total > 0 ? min(1, Double(step) / Double(total)) : 0,
                    testName: testName,
                    testIndex: testIndex,
                    testCount: testCount,
                    warmup: warmup,
                    phase: phase,
                    waitMs: waitMs,
                    attempt: attempt
                )
                Task { @MainActor in
                    guard self.isRunning else { return }
                    self.state = .running(info)
                }
            }
            Task { @MainActor in
                self.finish(json)
            }
        }
        thread.qualityOfService = .userInitiated
        thread.name = "MobileBench.runner"
        thread.start()
    }

    func cancel() {
        bridge?.cancel()
    }

    /// 計測中なら中止して、結果を無効にする。計測中でなければ何もしない
    private func handleLeftForeground() {
        guard isRunning else { return }
        bridge?.interrupt(withReason: MBInterruptLeftForeground)
    }

    /// 画面に指が触れた (TouchObserver から呼ばれる)。計測中だけ core が記録する (判定には使わない)
    func noteTouch() {
        guard isRunning else { return }
        bridge?.noteTouch()
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

    /// 端末の状態を iOS の公式 API で取得する (計測スレッドから呼ばれる。ProcessInfo はスレッドセーフ)。
    /// - thermal_state: nominal が通常。fair / serious / critical と上がるほど性能が制限される
    /// - low_power: 低電力モード
    /// MainActor の外で closure を作る (計測スレッドから呼ばれるため、MainActor に隔離させない)
    nonisolated private static func makeConditionProvider() -> @Sendable () -> [String: String] {
        { currentConditions() }
    }

    nonisolated private static func currentConditions() -> [String: String] {
        let info = ProcessInfo.processInfo
        let thermal: String
        switch info.thermalState {
        case .nominal: thermal = "nominal"
        case .fair: thermal = "fair"
        case .serious: thermal = "serious"
        case .critical: thermal = "critical"
        @unknown default: thermal = "unknown"
        }
        return [
            "thermal_state": thermal,
            "low_power": info.isLowPowerModeEnabled ? "true" : "false",
        ]
    }

    private static func deviceInfo() -> [String: String] {
        var systemInfo = utsname()
        uname(&systemInfo)
        let machine = withUnsafeBytes(of: &systemInfo.machine) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        let device = UIDevice.current
        var info = [
            "platform": "ios",
            "manufacturer": "Apple",
            "model": machine,  // 例: "iPhone15,2" (シミュレータでは "arm64" / "x86_64")
            "model_name": device.model,
            "os_version": device.systemVersion,
            // OS のビルド (docs/DESIGN.md D2)。表示用の文字列 (例 "Version 18.1 (Build 22B83)") も残す
            "os_version_string": ProcessInfo.processInfo.operatingSystemVersionString,
            "app_version": AppInfo.version,
            "app_build": AppInfo.build,
        ]
        // ビルド番号 (例 "22B83")。kern.osversion で取れると考えられるが、実機では未確認
        if let build = sysctlString("kern.osversion") { info["os_build"] = build }
        return info
    }

    private static func sysctlString(_ name: String) -> String? {
        var size = 0
        guard sysctlbyname(name, nil, &size, nil, 0) == 0, size > 0 else { return nil }
        var buffer = [CChar](repeating: 0, count: size)
        guard sysctlbyname(name, &buffer, &size, nil, 0) == 0 else { return nil }
        let bytes = buffer.prefix(while: { $0 != 0 }).map { UInt8(bitPattern: $0) }
        let value = String(decoding: bytes, as: UTF8.self)
        return value.isEmpty ? nil : value
    }
}

enum AppInfo {
    static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    /// CI では GitHub Actions の実行番号 (ios.yml で CURRENT_PROJECT_VERSION に渡している)
    static var build: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
    }
}
