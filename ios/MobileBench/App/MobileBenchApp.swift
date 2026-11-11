import SwiftUI

@main
struct MobileBenchApp: App {
    @StateObject private var viewModel = BenchmarkViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView(viewModel: viewModel)
        }
    }
}
