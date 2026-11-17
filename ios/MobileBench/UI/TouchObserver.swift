import SwiftUI
import UIKit
import UIKit.UIGestureRecognizerSubclass

/// 画面に指が触れたことを知らせる部品 (docs/PHASE1_PLAN.md P3.11)。
///
/// 置いたビューのウィンドウに、触れ始めを見るだけのジェスチャ認識を付ける。ウィンドウに付けた認識は、
/// ウィンドウの中のどのビューへのタッチも受け取る (UIGestureRecognizer の資料: 認識を付けたビューと、
/// その子のビューに当たったタッチを見る)。
/// 認識は、触れ始め (touchesBegan) で知らせた直後に失敗 (.failed) になる。cancelsTouchesInView を false、
/// delaysTouchesBegan を false (既定) にしているので、ボタンやスクロールへのタッチの配送は変わらない。
/// iOS の実機では確かめていない (docs/PHASE1_PLAN.md J12)。
struct TouchObserver: UIViewRepresentable {
    let onTouchBegan: () -> Void

    func makeUIView(context: Context) -> TouchObserverView {
        let view = TouchObserverView()
        view.isUserInteractionEnabled = false  // このビュー自体はタッチを受けない
        view.onTouchBegan = onTouchBegan
        return view
    }

    func updateUIView(_ uiView: TouchObserverView, context: Context) {
        uiView.onTouchBegan = onTouchBegan
    }
}

final class TouchObserverView: UIView {
    var onTouchBegan: (() -> Void)?
    private lazy var recognizer: TouchBeganRecognizer = {
        let r = TouchBeganRecognizer(target: nil, action: nil)
        r.onTouchBegan = { [weak self] in self?.onTouchBegan?() }
        return r
    }()

    override func didMoveToWindow() {
        super.didMoveToWindow()
        // 前のウィンドウから外し、今のウィンドウに付け直す
        recognizer.view?.removeGestureRecognizer(recognizer)
        window?.addGestureRecognizer(recognizer)
    }
}

/// 触れ始めを知らせて、すぐに失敗するジェスチャ認識。ほかのジェスチャやビューの操作を妨げない
final class TouchBeganRecognizer: UIGestureRecognizer {
    var onTouchBegan: (() -> Void)?

    override init(target: Any?, action: Selector?) {
        super.init(target: target, action: action)
        cancelsTouchesInView = false
        delaysTouchesBegan = false
        delaysTouchesEnded = false
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        super.touchesBegan(touches, with: event)
        // 1 回の操作 (最初の指が触れてから全部の指が離れるまで) で 1 回だけ知らせる。
        // 失敗した認識は、全部の指が離れると .possible に戻り、次の操作でまた呼ばれる
        if state == .possible {
            onTouchBegan?()
            state = .failed
        }
    }
}
