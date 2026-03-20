# if !defined( __structo_src_queries_rich_rankers_hpp__ )
# define __structo_src_queries_rich_rankers_hpp__
# include "field-set.hpp"
# include "context/text-image.hpp"
# include <vector>

namespace structo {
namespace queries {

  class TermRanker
  {
    struct RankerData
    {
      int                     refsCount = 0;
      std::vector<unsigned>   tagOffset;
      std::vector<double>     fidWeight;
    };

  public:
    TermRanker() = default;
    TermRanker( const TermRanker& );
    TermRanker& operator=( const TermRanker& );
    TermRanker( const FieldSet&, const context::Lexeme&, double, bool strict );

  // rank
    double  operator()( unsigned tag, uint8_t fid ) const;

  protected:
    RankerData* ranker = nullptr;

  };

  // TermRanker inline implementation

  inline  TermRanker::TermRanker( const TermRanker& src )
  {
    if ( (ranker = src.ranker) != nullptr )
      ++ranker->refsCount;
  }

  inline  TermRanker& TermRanker::operator=( const TermRanker& src )
  {
    if ( this != &src )
    {
      if ( ranker != nullptr && --ranker->refsCount == 0 )
        delete ranker;
      if ( (ranker = src.ranker) != nullptr )
        ++ranker->refsCount;
    }
    return *this;
  }

  inline  auto  TermRanker::operator()( unsigned tag, uint8_t fid ) const -> double
  {
    if ( ranker != nullptr && tag < ranker->tagOffset.size() && (tag = ranker->tagOffset[tag]) != unsigned(-1) )
      return ranker->fidWeight[tag + fid];
    return 0.2;
  }

}}

# endif   // !__structo_src_queries_rich_rankers_hpp__
