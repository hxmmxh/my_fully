#ifndef HXMMXH_MALLOC_H
#define HXMMXH_MALLOC_H

#include <malloc.h>
#include <assert.h>
#include <cstring>



namespace fool
{

  inline void *smartRealloc(void *p, const size_t currentSize, const size_t currentCapacity, const size_t newCapacity)
  {
    assert(p);
    assert(currentSize <= currentCapacity &&
           currentCapacity < newCapacity);
    // 判断空闲的空间
    auto const slack = currentCapacity - currentSize;
    // 如果空闲空间太大，用malloc分配个新的空间，只把有用的数据拷贝过去
    if (slack * 2 > currentSize)
    {
      // auto会忽略顶层const和引用,如果确实需要，显示指出const
      auto const result = malloc(newCapacity);
      std::memcpy(result, p, currentSize);
      free(p);
      return result;
    }
    // 因为realloc会把原来的内容全部拷贝到新的地址
    // 如果空闲的空间太大的话，浪费时间
    return realloc(p, newCapacity);
  }

}





// 有空时去完善throw_exception
/*
inline void* checkedMalloc(size_t size) {
  void* p = malloc(size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void* checkedCalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void* checkedRealloc(void* ptr, size_t size) {
  void* p = realloc(ptr, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void sizedFree(void* ptr, size_t size) {
  if (canSdallocx()) {
    sdallocx(ptr, size, 0);
  } else {
    free(ptr);
  }
}
*/



























#endif