#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// core からの進捗通知。計測スレッド (バックグラウンド) から呼ばれる。
typedef void (^MBProgressHandler)(NSInteger step, NSInteger totalSteps, NSInteger testIndex, NSInteger testCount,
                                  NSString *testName, BOOL warmup);

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

+ (NSString *)benchmarkVersion;

@end

NS_ASSUME_NONNULL_END
