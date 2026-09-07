# if !defined( __structo_src_indexer_dynamic_chains_ringbuffer_hxx__ )
# define __structo_src_indexer_dynamic_chains_ringbuffer_hxx__
# include <mtc/ptr.h>
# include <cstddef>
# include <atomic>
# include <thread>

# if defined(__x86_64__) || defined(_M_X64)
#   include <immintrin.h>
#   define PLATFORM_PAUSE() _mm_pause()
# elif defined(__arm__) || defined(__aarch64__)
#   define PLATFORM_PAUSE() __asm__ __volatile__("yield" ::: "memory")
# else
#   define PLATFORM_PAUSE() std::this_thread::yield()
# endif

namespace structo {
namespace indexer {
namespace dynamic {

  template <class T, size_t N>
  class RingBuffer
  {
    static_assert( (N & (N - 1)) == 0, "RingBuffer size has to be power of two!" );

    struct alignas(64) AtomicValue
    {
      T                   value;
      std::atomic<size_t> steps;
    };

    using AtomicPlace = std::atomic<size_t>;

    alignas(64) AtomicValue   buffer[N];
    alignas(64) AtomicPlace   putPos{0};
    alignas(64) AtomicPlace   getPos{0};

  public:
    RingBuffer()
    {
      for ( size_t i = 0; i != N; ++i )
        buffer[i].steps.store( i, std::memory_order_relaxed );
    }
    void  Put( T t )
    {
      auto  putIdx = putPos.fetch_add( 1, std::memory_order_relaxed );

      auto& toCell = buffer[putIdx & (N - 1)];
      for ( auto nloops = 0; toCell.steps.load( std::memory_order_acquire ) != putIdx; ++nloops )
      {
        if ( nloops < 64 )
        {
          PLATFORM_PAUSE();
        }
          else
        if ( nloops < 512 )
        {
          for ( int i = 0; i < (nloops >> 1); ++i )
            PLATFORM_PAUSE();
        }
          else
        {
          std::this_thread::sleep_for( std::chrono::microseconds(1) );
          nloops = 64;
        }
      }

      toCell.value = std::move( t );
      toCell.steps.store( putIdx + 1, std::memory_order_release );
    }
    bool  Get( T& tvalue )
    {
      auto  getIdx = getPos.load( std::memory_order_relaxed );
      auto& atCell = buffer[getIdx & (N - 1)];

      if ( atCell.steps.load( std::memory_order_acquire ) != getIdx + 1 )
        return false;

      tvalue = std::move( atCell.value );
        atCell.steps.store( getIdx + N, std::memory_order_release );

      return getPos.store( getIdx + 1, std::memory_order_relaxed ), true;
    }
  };

}}}

# endif   // !__structo_src_indexer_dynamic_chains_ringbuffer_hxx__
