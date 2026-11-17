#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// interruptWithReason: の理由: 計測中にアプリが前面を離れた (core の mb::kInterruptLeftForeground と同じ値)
/// (C++ から読み込んでも C のリンケージになるよう FOUNDATION_EXPORT で宣言する)
FOUNDATION_EXPORT NSString *const MBInterruptLeftForeground;

/// core からの進捗通知。計測スレッド (バックグラウンド) から呼ばれる。
/// phase: "prepare" / "peak" / "sustained" / "diagnostics"
/// waitMs: 回復待ち・事前負荷の経過時間 (それ以外は -1)、attempt: 測り直しの回数 (1 始まり)
typedef void (^MBProgressHandler)(NSInteger step, NSInteger totalSteps, NSInteger testIndex, NSInteger testCount,
                                  NSString *testName, BOOL warmup, NSString *phase, NSInteger waitMs,
                                  NSInteger attempt);

/// core の mb::Runner を包む Objective-C++ ブリッジ。計測ロジックは持たない。
/// run と cancel を別スレッドから呼んでよい (cancel は core 側で atomic)。
NS_SWIFT_SENDABLE
@interface MBBenchmarkBridge : NSObject

/// diagnostics: 診断モード (行列サイズ掃引など。スコアには影響しない)
- (instancetype)initWithDeviceInfo:(NSDictionary<NSString *, NSString *> *)deviceInfo
                        scratchDir:(NSString *)scratchDir
                       diagnostics:(BOOL)diagnostics NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

/// 端末の状態 (発熱状態・低電力モードなど) を返す。各テストの開始・終了時に計測スレッドから呼ばれる。
/// run の前に設定すること。
@property(nonatomic, copy, nullable) NSDictionary<NSString *, NSString *> * (^conditionProvider)(void);

/// 計測を実行し、core が生成した JSON を返す。呼び出しスレッドをブロックする。
- (NSString *)runWithProgress:(nullable MBProgressHandler)progress;

/// 任意のスレッドから呼んでよい。
- (void)cancel;

/// 計測を中止し、結果を無効にする (利用者の中止 cancel とは区別して記録される)。任意のスレッドから呼んでよい。
/// 計測中にアプリが前面を離れたら、reason に MBInterruptLeftForeground を渡す (docs/PHASE1_PLAN.md P3.10)
- (void)interruptWithReason:(NSString *)reason;

/// 画面に指が触れたときに呼ぶ。計測中だけ core が記録する (判定には使わない)。任意のスレッドから呼んでよい
- (void)noteTouch;

+ (NSString *)benchmarkVersion;

@end

NS_ASSUME_NONNULL_END
