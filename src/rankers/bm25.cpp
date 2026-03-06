# include "../../rankers.hpp"

namespace structo {
namespace rankers {

  const double k1 = 1.5;
  const double b1 = 0.75;

  auto  BM25( const queries::Abstract& tuples ) -> double
  {
    auto  score = double(0.0);

    for ( auto& next: tuples.factors )
    {
      auto  tmfreq = 1.0 * next.occurs / tuples.nWords;

      score += next.dblIDF * (tmfreq * (1 + k1)) / (tmfreq + 1.5 * (1 - b1 + b1 * tuples.nWords / 1000.0));
    }

    return score;
  }

}}
