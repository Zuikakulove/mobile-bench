// core の CLI が出した JSON を、iOS アプリのパーサ (ios/MobileBench/Model/BenchmarkResult.swift) に読ませる検査。
// docs/PHASE1_PLAN.md J7 の 3。iOS の実機がない間、パーサを確かめる唯一の手段になる。
//
// .github/workflows/ios.yml が、BenchmarkResult.swift とこのファイルを swiftc でコンパイルして実行する:
//   swift_parse_check <JSON のパス>
// 読めない (例外が出る) か、tests / summary_parts / diagnostics.matmul_sweep が空なら、終了コード 1 で終わる。
import Foundation

let args = CommandLine.arguments
guard args.count == 2 else {
    FileHandle.standardError.write("usage: swift_parse_check <json>\n".data(using: .utf8)!)
    exit(2)
}

do {
    let text = try String(contentsOfFile: args[1], encoding: .utf8)
    let result = try BenchmarkResult.parse(text)
    var errors: [String] = []
    if result.tests.isEmpty { errors.append("tests is empty") }
    if (result.summaryParts ?? []).filter({ !$0.isEmpty }).isEmpty { errors.append("summary_parts is empty") }
    if (result.diagnostics?.matmulSweep ?? []).isEmpty { errors.append("diagnostics.matmul_sweep is empty") }
    print("benchmark_version \(result.benchmarkVersion), tests \(result.tests.count), " +
          "summary_parts \(result.summaryParts?.count ?? 0), matmul_sweep \(result.diagnostics?.matmulSweep.count ?? 0), " +
          "build \(result.build.os) \(result.build.arch) \(result.build.compiler)")
    if !errors.isEmpty {
        for e in errors { print("ERROR: \(e)") }
        exit(1)
    }
    print("OK: BenchmarkResult.parse read the CLI JSON")
} catch {
    // DecodingError は、読めなかったキーのパスを含む
    print("ERROR: BenchmarkResult.parse failed: \(error)")
    exit(1)
}
