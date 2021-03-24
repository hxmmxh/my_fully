#include "hstring.h"
#include "hmalloc.h"
#include "likely.h"

#include <assert.h>

using namespace fool;

template <class InIt, class OutIt>
std::pair<InIt, OutIt> hstring_detail::copy_n(InIt b, typename std::iterator_traits<InIt>::difference_type n, OutIt d)
{
    for (; n != 0; --n, ++b, ++d)
    {
        *d = *b;
    }
    return std::make_pair(b, d);
}

template <class Pod, class T>
void hstring_detail::podFill(Pod *b, Pod *e, T c)
{
    assert(b && e && b <= e);
    constexpr auto kUseMemset = sizeof(T) == 1;
    if (kUseMemset)
    {
        memset(b, c, size_t(e - b));
    }
    else
    {
        auto const ee = b + ((e - b) & ~7u);
        for (; b != ee; b += 8)
        {
            b[0] = c;
            b[1] = c;
            b[2] = c;
            b[3] = c;
            b[4] = c;
            b[5] = c;
            b[6] = c;
            b[7] = c;
        }
        // Leftovers
        for (; b != e; ++b)
        {
            *b = c;
        }
    }
}

template <class Pod>
void hstring_detail::podCopy(const Pod *b, const Pod *e, Pod *d)
{
    assert(b != nullptr);
    assert(e != nullptr);
    assert(d != nullptr);
    assert(e >= b);
    assert(d >= e || d + (e - b) <= b);
    memcpy(d, b, (e - b) * sizeof(Pod));
}

template <class Pod>
void hstring_detail::podMove(const Pod *b, const Pod *e, Pod *d)
{
    assert(e >= b);
    memmove(d, b, (e - b) * sizeof(*b));
}

/*------------------------------------------MediumLarge------------------------------------------------------------------------------*/

size_t hstring_core::MediumLarge::capacity() const
{
    // 小端法，和掩码并
    // 大端法，右移两位就行了
    return kIsLittleEndian ? capacity_ & capacityExtractMask : capacity_ >> 2;
}

void hstring_core::MediumLarge::setCapacity(size_t cap, Category cat)
{
    // 小端法，把category左移到最高位的那个字节，也就是地址最高的字节
    // 大端法，就把cap左移两位，把最低位的两个Bit空出来
    capacity_ = kIsLittleEndian
                    ? cap | (static_cast<size_t>(cat) << kCategoryShift)
                    : (cap << 2) | static_cast<size_t>(cat);
}

/*------------------------------------------RefCounted------------------------------------------------------------------------------*/

constexpr size_t hstring_core::RefCounted::getDataOffset()
{
    return offsetof(RefCounted, data_);
}

hstring_core::RefCounted *hstring_core::RefCounted::fromData(char *p)
{
    return static_cast<RefCounted *>((static_cast<void *>(p)) - getDataOffset());
}

size_t hstring_core::RefCounted::refs(char *p)
{
    return fromData(p)->refCount_.load(std::memory_order_acquire);
}

void hstring_core::RefCounted::incrementRefs(char *p)
{
    fromData(p)->refCount_.fetch_add(1, std::memory_order_acq_rel);
}

void hstring_core::RefCounted::decrementRefs(char *p)
{
    auto const dis = fromData(p);
    // 返回的是旧值
    size_t oldcnt = dis->refCount_.fetch_sub(1, std::memory_order_acq_rel);
    assert(oldcnt > 0);
    if (oldcnt == 1)
    {
        free(dis);
    }
}

hstring_core::RefCounted *hstring_core::RefCounted::create(size_t *size)
{
    const size_t allocSize = getDataOffset() + (*size + 1) * sizeof(char);
    auto result = static_cast<RefCounted *>(malloc(allocSize));
    result->refCount_.store(1, std::memory_order_release);
    *size = (allocSize - getDataOffset()) / sizeof(char) - 1;
    return result;
}

hstring_core::RefCounted *hstring_core::RefCounted::create(const char *data, size_t *size)
{
    const size_t effectiveSize = *size;
    auto result = create(size);
    if (FOOL_LIKELY(effectiveSize > 0))
    {
        hstring_detail::podCopy(data, data + effectiveSize, result->data_);
    }
    return result;
}

hstring_core::RefCounted *hstring_core::RefCounted::reallocate(char *const data, const size_t currentSize, const size_t currentCapacity, size_t *newCapacity)
{
    assert(*newCapacity > 0 && *newCapacity > currentSize);
    // 形参中的newCapacity只是字符串的大小，需要加上\0和RefCounted结构体的大小
    const size_t allocNewCapacity = getDataOffset() + (*newCapacity + 1) * sizeof(char);
    auto const dis = fromData(data);
    assert(dis->refCount_.load(std::memory_order_acquire) == 1);
    // 把整个结构体重新分配内存
    auto result = static_cast<RefCounted *>(smartRealloc(
        dis,
        getDataOffset() + (currentSize + 1) * sizeof(char),
        getDataOffset() + (currentCapacity + 1) * sizeof(char),
        allocNewCapacity));
    assert(result->refCount_.load(std::memory_order_acquire) == 1);
    *newCapacity = (allocNewCapacity - getDataOffset()) / sizeof(char) - 1;
    return result;
}

/*------------------------------------------------构造和析构函数------------------------------------------------------------------------*/

hstring_core::hstring_core(const hstring_core &rhs)
{
    assert(&rhs != this);
    // 根据不同的类型，调用不同的方法
    switch (rhs.category())
    {
    case Category::isSmall:
        copySmall(rhs);
        break;
    case Category::isMedium:
        copyMedium(rhs);
        break;
    case Category::isLarge:
        copyLarge(rhs);
        break;
    default:
        break;
    }
    assert(size() == rhs.size());
    assert(memcmp(data(), rhs.data(), size() * sizeof(char)) == 0);
}

hstring_core::hstring_core(hstring_core &&goner) noexcept
{
    ml_ = goner.ml_;
    goner.reset();
}

hstring_core::hstring_core(const char *const data, const size_t size)
{
    // 根据字符串的大小调用不同方法
    if (size <= maxSmallSize)
    {
        initSmall(data, size);
    }
    else if (size <= maxMediumSize)
    {
        initMedium(data, size);
    }
    else
    {
        initLarge(data, size);
    }
    assert(this->size() == size);
    assert(size == 0 || memcmp(this->data(), data, size * sizeof(char)) == 0);
}

hstring_core::hstring_core(char *const data, const size_t size, const size_t allocatedSize, AcquireMallocatedString)
{
    if (size > 0)
    {
        assert(allocatedSize >= size + 1);
        assert(data[size] == '\0');
        // 用中字符串的存储模式
        ml_.data_ = data;
        ml_.size_ = size;
        // -1是为了去除\0
        ml_.setCapacity(allocatedSize - 1, Category::isMedium);
    }
    else
    {
        // size为0，释放data指向的空间，自身也初始化为空
        free(data);
        reset();
    }
}

hstring_core::~hstring_core() noexcept
{
    // 如果是小字符串，空间都在栈上，没有在堆里，不需要什么操作
    if (category() == Category::isSmall)
    {
        return;
    }
    destroyMediumLarge();
}

void hstring_core::destroyMediumLarge() noexcept
{
    auto const c = category();
    assert(c != Category::isSmall);
    if (c == Category::isMedium)
    {
        free(ml_.data_);
    }
    else
    {
        RefCounted::decrementRefs(ml_.data_);
    }
}

/*------------------------------------------------初始化数据函数------------------------------------------------------------------------*/

void hstring_core::initSmall(const char *const data, const size_t size)
{
    // hstring_core的布局为Char* data_, size_t size_, size_t capacity_
    // hstring_core的大小也就是MediumLarge的大小，一个char*和两个size_t
    static_assert(sizeof(*this) == sizeof(char *) + 2 * sizeof(size_t), "fbstring has unexpected size");
    static_assert(sizeof(char *) == sizeof(size_t), "fbstring size assumption violation");
    // sizeof(size_t) must be a power of 2
    static_assert((sizeof(size_t) & (sizeof(size_t) - 1)) == 0, "fbstring size assumption violation");

    // 如果传入的字符串地址是内存对齐的，则配合 reinterpret_cast 进行 word-wise copy，提高效率。
    if ((reinterpret_cast<size_t>(data) & (sizeof(size_t) - 1)) == 0)
    {
        const size_t byteSize = size * sizeof(char);
        constexpr size_t wordWidth = sizeof(size_t);
        // 求出size占多少字节，因为最大23，最多只占3字节
        switch ((byteSize + wordWidth - 1) / wordWidth)
        {
        case 3:
            // 最后一个字节是capacity
            ml_.capacity_ = reinterpret_cast<const size_t *>(data)[2];
            [[fallthrough]]; //指示从前一标号直落是有意的，而在发生直落时给出警告的编译器不应诊断
        case 2:
            // 第二个字节是size
            ml_.size_ = reinterpret_cast<const size_t *>(data)[1];
            [[fallthrough]];
        case 1:
            // 第一个字节是data
            ml_.data_ = *reinterpret_cast<char **>(const_cast<char *>(data));
            [[fallthrough]];
        case 0:
            break;
        }
    }
    else
    {
        if (size != 0)
        {
            hstring_detail::podCopy(data, data + size, small_);
        }
    }

    setSmallSize(size);
}

void hstring_core::initMedium(const char *const data, const size_t size)
{
    auto const allocSize = (1 + size) * sizeof(char);
    ml_.data_ = static_cast<char *>(malloc(allocSize));
    if (FOOL_LIKELY(size > 0))
    {
        hstring_detail::podCopy(data, data + size, ml_.data_);
    }
    ml_.size_ = size;
    ml_.setCapacity(allocSize / sizeof(char) - 1, Category::isMedium);
    ml_.data_[size] = '\0';
}

void hstring_core::initLarge(const char *const data, const size_t size)
{
    size_t effectiveCapacity = size;
    auto const newRC = RefCounted::create(data, &effectiveCapacity);
    ml_.data_ = newRC->data_;
    ml_.size_ = size;
    ml_.setCapacity(effectiveCapacity, Category::isLarge);
    ml_.data_[size] = '\0';
}

/*------------------------------------------------拷贝数据函数------------------------------------------------------------------------*/

void hstring_core::copySmall(const hstring_core &rhs)
{
    // hstring_core的布局为Char* data_, size_t size_, size_t capacity_
    static_assert(offsetof(MediumLarge, data_) == 0, "fbstring layout failure");
    static_assert(offsetof(MediumLarge, size_) == sizeof(ml_.data_), "fbstring layout failure");
    static_assert(offsetof(MediumLarge, capacity_) == 2 * sizeof(ml_.data_), "fbstring layout failure");
    // 简单粗暴，直接拷贝过来
    ml_ = rhs.ml_;
    assert(category() == Category::isSmall && this->size() == rhs.size());
}

void hstring_core::copyMedium(const hstring_core &rhs)
{
    // 执行一次深拷贝
    auto const allocSize = (1 + rhs.ml_.size_) * sizeof(char);
    // 分配空间
    ml_.data_ = static_cast<char *>(malloc(allocSize));
    // 把结尾的/0也拷贝进去
    hstring_detail::podCopy(rhs.ml_.data_, rhs.ml_.data_ + rhs.ml_.size_ + 1, ml_.data_);
    ml_.size_ = rhs.ml_.size_;
    ml_.setCapacity(allocSize / sizeof(char) - 1, Category::isMedium);
    assert(category() == Category::isMedium);
}

void hstring_core::copyLarge(const hstring_core &rhs)
{
    // ROW,增加一次引用计数就行了，data指向同一个地址
    ml_ = rhs.ml_;
    RefCounted::incrementRefs(ml_.data_);
    assert(category() == Category::isLarge && size() == rhs.size());
}

/*------------------------------------------------获取数据函数------------------------------------------------------------------------*/
void hstring_core::swap(hstring_core &rhs)
{
    auto const t = ml_;
    ml_ = rhs.ml_;
    rhs.ml_ = t;
}
const char *hstring_core::data() const { return c_str(); }

char *hstring_core::data() { return c_str(); }

char *hstring_core::mutableData()
{
    switch (category())
    {
    case Category::isSmall:
        return small_;
    case Category::isMedium:
        return ml_.data_;
    case Category::isLarge:
        return mutableDataLarge();
    }
    __builtin_unreachable();
}

const char *hstring_core::c_str() const
{
    const char *ptr = ml_.data_;
    // 提示编译器生成 CMOV 指令
    // 条件传送。类似于 MOV 指令，但是依赖于 RFLAGS 寄存器内的状态。如果条件没有满足，该指令不会有任何效果。
    // CMOV 的优点是可以避免分支预测，避免分支预测错误对 CPU 流水线的影响
    ptr = (category() == Category::isSmall) ? small_ : ptr;
    return ptr;
}

char *hstring_core::c_str()
{
    char *ptr = ml_.data_;
    ptr = (category() == Category::isSmall) ? small_ : ptr;
    return ptr;
}
/*--------------------------------------------------操纵字符串----------------------------------------------------------------------*/
void hstring_core::shrink(const size_t delta)
{
    if (category() == Category::isSmall)
    {
        shrinkSmall(delta);
    }
    // 放大字符串没有共享时，和中字符串一样的收缩方式
    else if (category() == Category::isMedium || RefCounted::refs(ml_.data_) == 1)
    {
        shrinkMedium(delta);
    }
    else
    {
        shrinkLarge(delta);
    }
}

void hstring_core::reserve(size_t minCapacity)
{
    switch (category())
    {
    case Category::isSmall:
        reserveSmall(minCapacity);
        break;
    case Category::isMedium:
        reserveMedium(minCapacity);
        break;
    case Category::isLarge:
        reserveLarge(minCapacity);
        break;
    default:
        __builtin_unreachable();
    }
    assert(capacity() >= minCapacity);
}

char *hstring_core::expandNoinit(const size_t delta, bool expGrowth)
{
    // 获取足够的空间，然后修改size就行了
    // 返回新增元素的首地址
    assert(capacity() >= size());
    size_t sz, newSz;
    if (category() == Category::isSmall)
    {
        sz = smallSize();
        newSz = sz + delta;
        if (FOOL_LIKELY(newSz <= maxSmallSize))
        {
            setSmallSize(newSz);
            return small_ + sz;
        }
        reserveSmall(expGrowth ? std::max(newSz, 2 * maxSmallSize) : newSz);
    }
    else
    {
        sz = ml_.size_;
        newSz = sz + delta;
        if (FOOL_UNLIKELY(newSz > capacity()))
        {
            // 扩容1.5倍
            reserve(expGrowth ? std::max(newSz, 1 + capacity() * 3 / 2) : newSz);
        }
    }
    assert(capacity() >= newSz);
    // 如果类型是小字符串，在前面就已经返回了
    assert(category() == Category::isMedium || category() == Category::isLarge);
    ml_.size_ = newSz;
    ml_.data_[newSz] = '\0';
    assert(size() == newSz);
    // 最后返回新分配的空间的首地址加上sz
    return ml_.data_ + sz;
}

void hstring_core::push_back(char c)
{
    *expandNoinit(1, true) = c;
}
/*------------------------------------------------------------------------------------------------------------------------*/
size_t hstring_core::smallSize() const
{
    assert(category() == Category::isSmall);
    // 小端法不需要移位，大端法时需要右移两位才能得到真实的size
    constexpr auto shift = kIsLittleEndian ? 0 : 2;
    auto smallShifted = static_cast<size_t>(small_[maxSmallSize]) >> shift;
    assert(static_cast<size_t>(maxSmallSize) >= smallShifted);
    // 实际存储的时maxSmallSize-size，最后还要换算一下
    return static_cast<size_t>(maxSmallSize) - smallShifted;
}

void hstring_core::setSmallSize(size_t s)
{
    assert(s <= maxSmallSize);
    constexpr auto shift = kIsLittleEndian ? 0 : 2;
    small_[maxSmallSize] = char((maxSmallSize - s) << shift);
    // 为了匹配原生的string,还是加上\0
    small_[s] = '\0';
    assert(category() == Category::isSmall && size() == s);
}

size_t hstring_core::size() const
{
    size_t ret = ml_.size_;
    ret = (category() == Category::isSmall) ? smallSize() : ret;
    return ret;
}

size_t hstring_core::capacity() const
{
    switch (category())
    {
    case Category::isSmall:
        return maxSmallSize;
    case Category::isLarge:
        if (RefCounted::refs(ml_.data_) > 1)
        {
            return ml_.size_;
        }
        break;
    case Category::isMedium:
    default:
        break;
    }
    return ml_.capacity();
}

void hstring_core::unshare(size_t minCapacity)
{
    assert(category() == Category::isLarge);
    size_t effectiveCapacity = std::max(minCapacity, ml_.capacity());
    // 新建一个引用计数
    auto const newRC = RefCounted::create(&effectiveCapacity);
    assert(effectiveCapacity >= ml_.capacity());
    // 把数据复制过去
    hstring_detail::podCopy(ml_.data_, ml_.data_ + ml_.size_ + 1, newRC->data_);
    // 原来的引用计数减一
    RefCounted::decrementRefs(ml_.data_);
    // 把现在对象的data指向分配的空间
    ml_.data_ = newRC->data_;
    // 设置容量，size没有改变，不需要重新设置
    ml_.setCapacity(effectiveCapacity, Category::isLarge);
}

char *hstring_core::mutableDataLarge()
{
    assert(category() == Category::isLarge);
    if (RefCounted::refs(ml_.data_) > 1)
    {
        unshare();
    }
    return ml_.data_;
}

void hstring_core::shrinkSmall(const size_t delta)
{
    assert(delta <= smallSize());
    setSmallSize(smallSize() - delta);
}

void hstring_core::shrinkMedium(const size_t delta)
{
    assert(ml_.size_ >= delta);
    ml_.size_ -= delta;
    ml_.data_[ml_.size_] = '\0';
}

void hstring_core::shrinkLarge(const size_t delta)
{
    assert(ml_.size_ >= delta);
    // 需要分配新的空间
    if (delta)
    {
        // 这个临时对象会在函数结束后析构
        // 会自动把原来共享对象的计数减一
        hstring_core(ml_.data_, ml_.size_ - delta).swap(*this);
    }
}

void hstring_core::reserveSmall(size_t minCapacity)
{
    assert(category() == Category::isSmall);
    //如果就是小字符串，不需要操作
    if (minCapacity <= maxSmallSize)
    {
    }
    // 要把实现方式改成中字符串
    else if (minCapacity <= maxMediumSize)
    {
        auto const allocSizeBytes = (1 + minCapacity) * sizeof(char);
        auto const pData = static_cast<char *>(malloc(allocSizeBytes));
        auto const size = smallSize();
        hstring_detail::podCopy(small_, small_ + size + 1, pData);
        ml_.data_ = pData;
        ml_.size_ = size;
        ml_.setCapacity(allocSizeBytes / sizeof(char) - 1, Category::isMedium);
    }
    // 要把实现方式改成大字符串
    else
    {
        auto const newRC = RefCounted::create(&minCapacity);
        auto const size = smallSize();
        hstring_detail::podCopy(small_, small_ + size + 1, newRC->data_);
        ml_.data_ = newRC->data_;
        ml_.size_ = size;
        ml_.setCapacity(minCapacity, Category::isLarge);
        assert(capacity() >= minCapacity);
    }
}

void hstring_core::reserveMedium(const size_t minCapacity)
{
    assert(category() == Category::isMedium);
    // String is not shared
    // 如果参数值小于当前容量，则什么都不做
    if (minCapacity <= ml_.capacity())
    {
        return;
    }
    // 还是中字符串模式
    if (minCapacity <= maxMediumSize)
    {

        size_t capacityBytes = (1 + minCapacity) * sizeof(char);
        ml_.data_ = static_cast<char *>(smartRealloc(
            ml_.data_,
            (ml_.size_ + 1) * sizeof(char),
            (ml_.capacity() + 1) * sizeof(char),
            capacityBytes));
        ml_.setCapacity(capacityBytes / sizeof(char) - 1, Category::isMedium);
    }
    else
    {
        // 否则需要把底层实现转化成大字符串
        hstring_core nascent;
        // 这里实际上会调用reserveSmall
        nascent.reserve(minCapacity);
        nascent.ml_.size_ = ml_.size_;
        hstring_detail::podCopy(ml_.data_, ml_.data_ + ml_.size_ + 1, nascent.ml_.data_);
        nascent.swap(*this);
        assert(capacity() >= minCapacity);
    }
}

void hstring_core::reserveLarge(size_t minCapacity)
{
    assert(category() == Category::isLarge);
    if (RefCounted::refs(ml_.data_) > 1)
    {
        unshare(minCapacity);
    }
    else
    {
        if (minCapacity > ml_.capacity())
        {
            auto const newRC = RefCounted::reallocate(ml_.data_, ml_.size_, ml_.capacity(), &minCapacity);
            ml_.data_ = newRC->data_;
            ml_.setCapacity(minCapacity, Category::isLarge);
        }
        assert(capacity() >= minCapacity);
    }
}
