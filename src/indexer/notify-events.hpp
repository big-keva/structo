# if !defined( __structo_src_indexer_notify_events_hxx__ )
# define __structo_src_indexer_notify_events_hxx__
# include <functional>

namespace structo {

  struct Notify final
  {
    enum class Event: unsigned
    {
      None = 0,
      OK = 1,
      Empty = 2,
      Canceled = 3,
      Failed = 4
    };

    using Func = std::function<void(void*, Event)>;

  };

}

# endif   // !__structo_src_indexer_notify_events_hxx__
