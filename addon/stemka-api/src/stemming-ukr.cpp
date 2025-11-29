# include "../../../lang-api.hpp"
# include "stemka/include/stemmers.h"
# include "stemka/include/fuzzyukr.h"
# include <moonycode/codes.h>

stemScan  uaScan = { GetUkrTables(), codepages::xlatOneToOne, GetUkrVowels(),
  (unsigned)-1 };

void  Stematize( structo::ILemmatizer::IWord* pword, const widechar* pws, const char* psz, size_t len )
{
  unsigned  ustems[0x10];
  int       nstems = GetStemLenBuffer( &uaScan, ustems, std::size(ustems), psz, len );

  for ( int i = 0; i != nstems; ++i )
    pword->AddStem( pws, ustems[i], 1U, 1.0 * ustems[i] / len, nullptr, 0 );
}
