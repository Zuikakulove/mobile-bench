import Foundation

/// core が返す JSON (schema_version 2) の表示用モデル。
/// スコア計算などは一切行わず、JSON の値をそのまま読むだけ。
struct BenchmarkResult: Decodable {
    struct Config: Decodable {
        let warmupRuns: Int
        let measuredRuns: Int
        let logicalCores: Int
    }

    struct Build: Decodable {
        let os: String
        let arch: String
        let compiler: String
        let flagsProfile: Int
        let flags: String
        let optimized: Bool
    }

    struct Worker: Decodable {
        let worker: Int
        let units: Int
        let busyNs: Int64
        let cpus: [Int]
        let ghzBefore: Double
        let ghzAfter: Double
    }

    struct RunDetail: Decodable {
        let warmup: Bool
        let ns: Int64
        let unitNs: [Int64]?
        let workers: [Worker]

        var ghzBefore: Double { Self.average(workers.map(\.ghzBefore)) }
        var ghzAfter: Double { Self.average(workers.map(\.ghzAfter)) }

        private static func average(_ values: [Double]) -> Double {
            let v = values.filter { $0 > 0 }
            return v.isEmpty ? 0 : v.reduce(0, +) / Double(v.count)
        }
    }

    struct TestEntry: Decodable, Identifiable {
        let id: String
        let name: String
        let threads: Int
        let medianNs: Int64
        let runsNs: [Int64]
        let score: Double
        let valid: Bool
        let error: String?
        let runDetails: [RunDetail]?
    }

    struct PerfLevel: Decodable {
        let name: String
        let logicalCpus: Int
        let l1dBytes: Int64
        let l2Bytes: Int64
        let cpusPerL2: Int
    }

    struct Topology: Decodable {
        let source: String
        let perfLevels: [PerfLevel]
    }

    struct SweepPoint: Decodable, Identifiable {
        let n: Int
        let threads: Int
        let stWorkingSetBytes: Int64
        let mtWorkingSetBytes: Int64
        let stGflops: Double
        let mtGflops: Double
        let valid: Bool
        var id: Int { n }
    }

    struct Diagnostics: Decodable {
        let matmulSweep: [SweepPoint]
    }

    let benchmarkVersion: String
    let overallScore: Double
    let valid: Bool
    let cancelled: Bool
    let config: Config
    let build: Build
    let tests: [TestEntry]
    let cpuTopology: Topology?
    let diagnostics: Diagnostics?

    private struct ErrorOnly: Decodable { let error: String? }

    static func parse(_ json: String) throws -> BenchmarkResult {
        let data = Data(json.utf8)
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        if let e = try? decoder.decode(ErrorOnly.self, from: data), let message = e.error {
            throw NSError(domain: "MobileBench", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
        }
        return try decoder.decode(BenchmarkResult.self, from: data)
    }

    static func formatBytes(_ bytes: Int64) -> String {
        if bytes < 0 { return "?" }
        if bytes >= 1024 * 1024 {
            return String(format: "%.1fMB", locale: Locale(identifier: "en_US_POSIX"), Double(bytes) / 1024 / 1024)
        }
        return "\(bytes / 1024)KB"
    }
}
