#import "MBBenchmarkBridge.h"

#include <exception>
#include <memory>
#include <string>

#include "mobilebench/runner.h"
#include "mobilebench/version.h"

namespace {

std::string ToStd(NSString *s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }

NSString *ToNS(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

NSString *ErrorJson(NSString *message) {
  NSData *data = [NSJSONSerialization dataWithJSONObject:@{@"error" : message ?: @"unknown"} options:0 error:nil];
  return data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"{\"error\":\"unknown\"}";
}

}  // namespace

@implementation MBBenchmarkBridge {
  std::unique_ptr<mb::Runner> _runner;
}

- (instancetype)initWithDeviceInfo:(NSDictionary<NSString *, NSString *> *)deviceInfo
                        scratchDir:(NSString *)scratchDir {
  if ((self = [super init])) {
    mb::RunConfig config;
    config.scratch_dir = ToStd(scratchDir);
    [deviceInfo enumerateKeysAndObjectsUsingBlock:^(NSString *key, NSString *value, BOOL *stop) {
      config.device[ToStd(key)] = ToStd(value);
    }];
    _runner = std::make_unique<mb::Runner>(std::move(config));
  }
  return self;
}

- (NSString *)runWithProgress:(MBProgressHandler)progress {
  try {
    const std::string json = _runner->run_json([progress](const mb::Progress &p) {
      if (progress) {
        progress(p.step, p.total_steps, p.test_index, p.test_count, ToNS(p.test_name), p.warmup);
      }
    });
    return ToNS(json);
  } catch (const std::exception &e) {
    return ErrorJson(@(e.what()));
  } catch (...) {
    return ErrorJson(@"unknown native error");
  }
}

- (void)cancel {
  _runner->cancel();
}

+ (NSString *)benchmarkVersion {
  return @(mb::kBenchmarkVersion);
}

@end
