#ifndef HXMMXH_LIKELY_H
#define HXMMXH_LIKELY_H

// 允许程序员将最有可能执行的分支告诉编译
// __builtin_expect(EXP, N).意思是：EXP==N的概率很大

#define FOOL_DETAIL_BUILTIN_EXPECT(b, t) (__builtin_expect(b, t))

#define FOOL_LIKELY(x) FOOL_DETAIL_BUILTIN_EXPECT((x), 1)
#define FOOL_UNLIKELY(x) FOOL_DETAIL_BUILTIN_EXPECT((x), 0)

#endif