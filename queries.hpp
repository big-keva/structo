# if !defined( __structo_queries_hpp__ )
# define __structo_queries_hpp__
# include "bounds.hpp"
# include <mtc/interfaces.h>
# include <initializer_list>
# include <mtc/arena.hpp>
# include <memory>

namespace structo {
namespace queries {

  struct Abstract
  {
    struct BM25Term
    {
      unsigned  termId;
      double    dblIDF;      // format zone weight
      unsigned  format;      // document format zone
      unsigned  occurs;      // term occurences
    };

    struct EntryPos
    {
      unsigned  offset;
      unsigned  termID;
    };

    struct EntrySet
    {
      struct Limits
      {
        unsigned uMin;
        union
        {
          unsigned uMax;
          EntryPos ePos;
        };
      };
      struct Spread
      {
        const EntryPos* pbeg;
        const EntryPos* pend;

        auto    begin() const -> const EntryPos*  {  return pbeg;  }
        auto    end() const -> const EntryPos*  {  return pend;  }
        bool    empty() const {  return pbeg == pend;  }
        size_t  size() const {  return pend - pbeg;  }
      };

      Limits  limits;
      double  weight;
      Spread  spread;
    };

    struct Entries
    {
      const EntrySet*  pbeg = nullptr;
      const EntrySet*  pend = nullptr;

      auto    begin() const -> const EntrySet*  {  return pbeg;  }
      auto    end() const -> const EntrySet*  {  return pend;  }
      bool    empty() const {  return pbeg == pend;  }
      size_t  size() const  {  return pend - pbeg;  }
      auto    back() const -> const EntrySet&  {  return pend[-1];  }
    };

    struct Factors
    {
      const BM25Term*  pbeg = nullptr;
      const BM25Term*  pend = nullptr;

      auto    begin() const -> const BM25Term*  {  return pbeg;  }
      auto    end() const -> const BM25Term*  {  return pend;  }
      bool    empty() const {  return pbeg == pend;  }
      size_t  size() const  {  return pend - pbeg;  }
    };

    enum: unsigned
    {
      None = 0,
      BM25 = 1,
      Rich = 2
    };

    unsigned  dwMode = None;
    unsigned  nWords = 0;     // общее количество слов в документе
    union
    {
      Entries entries;
      Factors factors;
    };
  };

  /*
  * IQuery
  *
  * Представление вычислителя поисковых запросов.
  *
  * Находит идентификаторы документов и возвращает структуры данных, годные для ранжирования.
  *
  * Временем жизни этих структур управляет вычислитель запросов, так что после перехода
  * к следующему документу данные структуры окажутся некорректными.
  */
  struct IQuery: mtc::Iface
  {
    virtual uint32_t          LastIndex() = 0;
    virtual uint32_t          SearchDoc( uint32_t ) = 0;
    virtual const Abstract&   GetTuples( uint32_t ) = 0;
    virtual mtc::api<IQuery>  Duplicate( const Bounds& = {} ) = 0;
  };

  auto  GetQuotation( const Abstract& ) -> Abstract::Entries;

  auto  MakeAbstract( mtc::Arena&, const std::initializer_list<Abstract::EntrySet>& ) -> Abstract;
  auto  MakeEntrySet( mtc::Arena&, const std::initializer_list<Abstract::EntryPos>&, double = 0.1 ) -> Abstract::EntrySet;
  auto  MakeEntrySet( mtc::Arena&, const std::initializer_list<unsigned>&, double = 0.1 ) -> Abstract::EntrySet;

  inline
  auto  MakeEntrySet( Abstract::EntrySet& ent, const Abstract::EntryPos& pos, double wht = 0.0 ) -> Abstract::EntrySet&
  {
    ent.limits.uMin = (ent.limits.ePos = pos).offset;
    ent.weight = wht;
    ent.spread = { &ent.limits.ePos, 1 + &ent.limits.ePos };
    return ent;
  }

}}

# endif   // !__structo_queries_hpp__
