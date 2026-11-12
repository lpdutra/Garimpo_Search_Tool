import Foundation
import GarimpoCore
import Observation

@MainActor
@Observable
final class ReplaceModel: Identifiable {
    enum State: Equatable {
        case preparing
        case ready
        case failed(String)
    }

    let id = UUID()
    let query: SearchQuery
    let replacement: String
    var state: State = .preparing
    var plan: ReplacePlan?
    @ObservationIgnored private var task: Task<Void, Never>?

    init(query: SearchQuery, replacement: String) {
        self.query = query
        self.replacement = replacement
    }

    /// Finds the matching files and builds the preview off the main thread.
    func prepare() {
        let query = query
        let replacement = replacement
        let limits = Preferences.limits
        task = Task {
            let result = await Task.detached(priority: .userInitiated) { () -> Result<ReplacePlan, Error> in
                do {
                    var paths: [String] = []
                    _ = try SearchEngine.search(query, limits: limits, cache: nil) { event in
                        if case .found(let files) = event { paths += files.filter { $0.kind == .text }.map(\.path) }
                    }
                    return .success(try ReplacePlanner.plan(query: query, replacement: replacement, paths: paths.sorted()))
                } catch {
                    return .failure(error)
                }
            }.value
            guard !Task.isCancelled else { return }
            switch result {
            case .success(let plan):
                self.plan = plan
                state = .ready
            case .failure(let error):
                state = .failed(error.localizedDescription)
            }
        }
    }

    func cancel() {
        task?.cancel()
    }

    func setAll(_ included: Bool) {
        guard var plan else { return }
        for index in plan.files.indices {
            plan.files[index].included = included
            for line in plan.files[index].lines.indices { plan.files[index].lines[line].included = included }
        }
        self.plan = plan
    }
}
