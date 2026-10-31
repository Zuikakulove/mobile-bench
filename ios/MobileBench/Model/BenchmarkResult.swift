import Foundation

/// core が返す JSON (schema_version 1) の表示用モデル。
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

    struct TestEntry: Decodable, Identifiable {
        let id: String
        let name: String
        let threads: Int
        let medianNs: Int64
        let runsNs: [Int64]
        let score: Double
        let valid: Bool
        let error: String?
    }

    let benchmarkVersion: String
    let overallScore: Double
    let valid: Bool
    let cancelled: Bool
    let config: Config
    let build: Build
    let tests: [TestEntry]

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
}
