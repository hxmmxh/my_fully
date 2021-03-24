
- [string常用的三种实现方式](#string常用的三种实现方式)
  - [直接拷贝, eager copy](#直接拷贝-eager-copy)
  - [COW, copy-on-write](#cow-copy-on-write)
  - [SSO, small string optimization](#sso-small-string-optimization)
- [fbstring概述](#fbstring概述)
  - [概述](#概述)
  - [组织架构](#组织架构)
  - [字符串存储数据结构](#字符串存储数据结构)
- [类型,大小，容量](#类型大小容量)
  - [设置category](#设置category)
  - [获取category](#获取category)
  - [获取size](#获取size)
  - [获取capacity](#获取capacity)
- [RefCounted,引用计数的实现](#refcounted引用计数的实现)
  - [<atomic>操作](#atomic操作)
- [初始化操作](#初始化操作)
---------------------------------------------------------------------------------------------------------------------
# string常用的三种实现方式

- string中比较重要的3个字段：
  - `char *data`, 指向存放字符串的首地址（在 SSO 的某些实现方案中可能没有此字段）。
  - `size_t size`, 字符串长度。
  - `size_t capacity`, 字符串容量。capacity >= size. 在字符串相加、reserve 等场景下会用到此字段。

## 直接拷贝, eager copy
- 每次拷贝时将原 string 对应的内存完整地复制一份，即没有任何特殊处理
- 实现简单。每个对象互相独立，不用考虑那么多乱七八糟的场景。
- 字符串较大时，拷贝浪费空间

## COW, copy-on-write
https://www.cnblogs.com/promise6522/archive/2012/03/22/2412686.html
- 只有在某个 string 要对共享对象进行修改时，才会真正执行拷贝
  - 拷贝字符串的时间为O(1)，但是拷贝后的第一次修改操作为O(N)
- 由于存在共享机制，所以需要一个`std::atomic<size_t> refcount`，代表被多少对象共享
- 字符串空间较大时，减少了分配、复制字符串的时间
- refcount 需要原子操作，性能有损耗
- 某些情况下会带来意外的开销。比如非 const 成员使用[]，这会触发 COW，因为无法知晓应用程序是否会对返回的字符做修改

```cpp
std::string s("str");  // 初始化时refcount=1
const char* p = s.data();
{
    std::string s2(s); // 此时refcount=2 
    (void) s[0];       // 触发COW，refcount=1
}//s2离开作用域 refount=0,原有地址析构
std::cout << *p << '\n';      // p指向的原有空间已经无效
```



## SSO, small string optimization
- 基于字符串大多数比较短的特点，利用 string 对象本身的栈空间来存储短字符串。而当字符串长度大于某个临界值时，则使用 eager copy 的方式
  - 字符串较短时，直接存放在对象的buffer中，start指向data.buffer
  - 字符串较长时，start指向堆上分配的空间，data.capacity表示容量
- 实现方式有很多种，主要区别是那三个值哪一个与本地缓冲重合
- 短字符串时，无动态内存分配。
- string 对象占用空间比 eager copy 和 cow 要大

```cpp
class string {
  char *start;
  size_t size;
  static const int kLocalSize = 15;
  union{
    char buffer[kLocalSize+1];      // 满足条件时，用来存放短字符串
    size_t capacity;
  }data;
};
```

# fbstring概述

## 概述

- 针对不同string大小选用不同的string实现方式
  - Small Strings (<= 23 chars) ，使用 SSO.
  - Medium strings (24 - 255 chars)，使用 eager copy.
  - Large strings (> 255 chars)，使用 COW.
- 优点
  - 与 std::string 100%兼容，可以与 std::string 互相转换。
  - COW 存储时对于引用计数线程安全。
  - 对 Jemalloc 友好。如果检测到使用 jemalloc，那么将使用 jemalloc 的一些非标准扩展接口来提高性能。
  - find()使用简化版的Boyer-Moore algorithm。在查找成功的情况下，相对于string::find()有 30 倍的性能提升。在查找失败的情况下也有 1.5 倍的性能提升。

## 组织架构

- `fdstring str("abc)`,提供给用户的接口为fbstring
- `typedef basic_fbstring<char> fbstring`,fbstring实际上为basic_fbstring的别名
- basic_fbstring实际上调用了fbstring_core 提供的接口
- fbstring_core 负责字符串的存储及字符串相关的操作

## 字符串存储数据结构

- 3 个数据结构`union{Char small*, MediumLarge ml*}`、`MediumLarge`、`RefCounted`
- small strings（SSO）时
  - 使用 union 中的 Char small_存储字符串，即对象本身的栈空间
  - SSO 的场景并不需要 capacity，因为此时利用的是栈空间，或者理解此种情况下的 capacity=maxSmallSize
  - 利用 small_的一个字节来存储 size(位于最后)，而且具体存储的不是 size，而是maxSmallSize - s
    - 这样做的原因是让small strings 可以多存储一个字节 。
    - 因为假如存储 size 的话，small中最后两个字节就得是\0 和 size，但是存储maxSmallSize - size，当 size == maxSmallSize 时，small的最后一个字节恰好也是\0。
  - 因为要利用最后一个字节存储size和category,所以SSO的最大长度为sizeof(ml_)-1,64位系统下位23
- medium strings（eager copy）时，使用 union 中的MediumLarge ml_
  - Char* data_ ： 指向分配在堆上的字符串。
  - size_t size：字符串长度。
  - size_t capacity ：字符串容量
- large strings（cow）时， 使用MediumLarge ml_和 RefCounted：
  - RefCounted.refCount_ ：共享字符串的引用计数。
  - RefCounted.data_[1] : flexible array. 存放字符串。
  - ml.data_指向 RefCounted.data，ml.size_与 ml.capacity_的含义不变。

```cpp
union {
    uint8_t bytes_[sizeof(MediumLarge)];          // For accessing the last byte.
    Char small_[sizeof(MediumLarge) / sizeof(Char)];
    MediumLarge ml_;
};

struct MediumLarge {
  Char* data_;
  size_t size_;
  size_t capacity_;

  size_t capacity() const {
    return kIsLittleEndian ? capacity_ & capacityExtractMask : capacity_ >> 2;
  }

  void setCapacity(size_t cap, Category cat) {
    capacity_ = kIsLittleEndian
        ? cap | (static_cast<size_t>(cat) << kCategoryShift)
        : (cap << 2) | static_cast<size_t>(cat);
  }
};

struct RefCounted {
    std::atomic<size_t> refCount_;
    Char data_[1];

    static RefCounted * create(size_t * size);       // 创建一个RefCounted
    static RefCounted * create(const Char * data, size_t * size);     // ditto
    static void incrementRefs(Char * p);     // 增加一个引用
    static void decrementRefs(Char * p);    // 减少一个引用

   // 其他函数定义
};

```

# 类型,大小，容量

- 因为只有三种类型，所以只需要 2 个 bit 就能够区分
- kIsLittleEndian 为判断当前平台的大小端，大端和小端的存储方式不同
- 大端小端回顾
  - 大端：数据的高字节保存在内存的低地址中
  - 小端：数据的高字节保存在内存的高地址中
```cpp
typedef uint8_t category_type;

enum class Category : category_type {
    isSmall = 0,
    isMedium = kIsLittleEndian ? 0x80 : 0x2,       //  10000000 , 00000010
    isLarge = kIsLittleEndian ? 0x40 : 0x1,        //  01000000 , 00000001
};
```
## 设置category

- 以下操作的目的，都是为了把表示category的两个bit放在这个Union的最后一个字节中
- ml_的最后一个字节，即最大的地址
  - 小端法表示时，是ml_的最高位
  - 大端法表示时，是ml_的最低位
- 两个Bit在字节中位置
  - 小端法的两个Bit放在那个字节的最左边，xx000000
  - 大端法的两个Bit放在那个字节的最右边，000000xx
```cpp
union {
    uint8_t bytes_[sizeof(MediumLarge)]; // For accessing the last byte.
    Char small_[sizeof(MediumLarge) / sizeof(Char)];
    MediumLarge ml_;
```

- 对于small strings，两个bit存放在small_的最后一个字节中
  - 在setsize时顺便设置了
  - small_的最后一个字节同时保存着size和category两个值
  - 因为size的值最大为23，一个字节有8位，可以分出2位用来记录category，其他6位记录size
  - 小端法时表示category的两个bit位于字节的最左边，因为size值最大位23，可以确保最左边两位为00
  - 大端法时位于右边。通过把size值左移两位，可以确保字节的最右边两位是00

```cpp
void setSmallSize(size_t s) {
  ......
  constexpr auto shift = kIsLittleEndian ? 0 : 2;
  small_[maxSmallSize] = char((maxSmallSize - s) << shift);
  ......
}
```
- 对于medium strings，两个bit存放在capacity_中
  - capacity_恰好是MediumLarge结构的最后一个成员，首先可以保证它的地址是ml_的最大地址
  - 小端时，将category = isMedium = 0x80 向左移动(sizeof(size_t) - 1) * 8位，即移到最高位中，再与 capacity 做或运算。
    - 让capacity_的最高位两位为10，因为小端法的最高位的字节位于最高地址
  - 大端时，将 category = isMedium = 0x2 与 cap << 2 做或运算即可，左移 2 位的目的是给 category 留空间。
    - 让capacity_的最低两位为10，大端法最低位有最高的地址
```cpp
constexpr static size_t kCategoryShift = (sizeof(size_t) - 1) * 8;

void setCapacity(size_t cap, Category cat) {
    capacity_ = kIsLittleEndian
        ? cap | (static_cast<size_t>(cat) << kCategoryShift)
        : (cap << 2) | static_cast<size_t>(cat);
}
```
- large strings
  - 同样使用 MediumLarge 的 setCapacity，算法相同，只是 category 的值不同。

## 获取category

- 通过bytes_[lastChar]获得union结构的最高地址的那个字节
- 然后再通过&运算得到category值
  - 大端法是最右边两位，掩码为00000011
  - 小端法是最左边两位，掩码为11000000

```cpp
constexpr static uint8_t categoryExtractMask = kIsLittleEndian ? 0xC0 : 0x3;    // 11000000 , 00000011
constexpr static size_t lastChar = sizeof(MediumLarge) - 1;

union {
    uint8_t bytes_[sizeof(MediumLarge)];          // For accessing the last byte.
    Char small_[sizeof(MediumLarge) / sizeof(Char)];
    MediumLarge ml_;
};

Category category() const {
  // works for both big-endian and little-endian
  return static_cast<Category>(bytes_[lastChar] & categoryExtractMask);
}
```


## 获取size

- medium strings和large strings直接返回ml_.size()
- small strings的size存放在samll_的最后一个字节中，并且存储的是maxSmallSize-size 

## 获取capacity

- small strings : 直接返回 maxSmallSize，前面有分析过。
- medium strings : 返回 ml_.capacity()。
- large strings :
  - 当字符串引用大于 1 时，直接返回 size。因为此时的 capacity 是没有意义的，任何 append data 操作都会触发一次 cow
  - 否则，返回 ml_.capacity()
- ml.capacity()
  - 目的就是消掉capacity_中category占的两位
  - 大端法时右移两位就行了
  - 小端法和一个掩码并



# RefCounted,引用计数的实现

## <atomic>操作





# 初始化操作