# include "rich-rankers.hpp"

namespace structo {
namespace queries {

  TermRanker::TermRanker( const FieldSet& fds, const context::Lexeme& lex, double flo, bool fmatch )
  {
    ++(ranker = new RankerData())->refsCount;

    for ( auto tag: fds )
    {
    // get tag relocation
      if ( ranker->tagOffset.size() <= tag )
        ranker->tagOffset.insert( ranker->tagOffset.end(), tag - ranker->tagOffset.size() + 1, uint32_t(-1) );
      if ( ranker->tagOffset[tag] == uint32_t(-1) )
        ranker->tagOffset[tag] = unsigned(ranker->fidWeight.size());

    // insert forms mapping
      if ( ranker->fidWeight.size() <= ranker->tagOffset[tag] + 256 )
        ranker->fidWeight.insert( ranker->fidWeight.end(), ranker->tagOffset[tag] + 256 - ranker->fidWeight.size(), -1.0 );

    // fill default value
      if ( fmatch )
        std::fill( ranker->fidWeight.begin() + ranker->tagOffset[tag], ranker->fidWeight.begin() + ranker->tagOffset[tag] + 256, -1 );
      else
        std::fill( ranker->fidWeight.begin() + ranker->tagOffset[tag], ranker->fidWeight.begin() + ranker->tagOffset[tag] + 256, 0.5 * flo );

      if ( lex.GetForms().empty() )
      {
        ranker->fidWeight[255 + ranker->tagOffset[tag]] = flo;
      }
        else
      {
        for ( auto fid: lex.GetForms() )
          ranker->fidWeight[fid + ranker->tagOffset[tag]] = flo;
      }
    }
  }

}}
