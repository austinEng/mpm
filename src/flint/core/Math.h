

namespace core {
    
template <typename T>
constexpr bool IsPowerOfTwo(T n) {
    return (n != 0) && ((n & (n - 1)) == 0);
}

template <unsigned int Alignment, typename T>
constexpr T* Align(T* ptr) {
    static_assert(IsPowerOfTwo(Alignment), "Alignment must be a power of two");
    return reinterpret_cast<T*>((reinterpret_cast<size_t>(ptr) + (Alignment - 1)) & ~(Alignment - 1));
}

template <unsigned int Alignment, typename T>
constexpr T Align(T value) {
    static_assert(IsPowerOfTwo(Alignment), "Alignment must be a power of two");
    T alignment = static_cast<T>(Alignment);
    return (value + (alignment - 1)) & ~(alignment - 1);
}
    
}