# include "../../context/x-contents.hpp"
# include "../../context/pack-format.hpp"
# include "../../compat.hpp"
# include <mtc/arbitrarymap.h>
# include <mtc/arena.hpp>

namespace structo {
namespace context {

  class StubFields: public FieldHandler
  {
    auto  Add( const std::string_view& ) ->             FieldOptions* override {  return nullptr;  }
    auto  Get( const std::string_view& ) const -> const FieldOptions* override {  return nullptr;  }
    auto  Get( unsigned                ) const -> const FieldOptions* override {  return nullptr;  }
  };

  static  StubFields stubFields;

  struct Contents::impl
  {
    std::vector<EntryView> entryViews;
  };

 /*
  * MiniImpl - просто факт присутствия лексемы в документе и его размерность.
  */
  struct MiniImpl: Contents::impl
  {
    mtc::arbitrarymap<size_t> keyMapping;
    char                      docSizeBuf[0x20];
  };

 /*
  * BM25Impl - количество появлений ключа в документе и размерность этого документа.
  */
  struct BM25Impl: public Contents::impl
  {
    struct CountRec
    {
      size_t  keypos;
      char    serial[8];
    };

    mtc::arbitrarymap<CountRec> keyMapping;
    char                        docSizeBuf[0x20];
  };

 /*
  * RichImpl - предельно подробная детализация вхождений, и форматирование вдогонку.
  */
  class RichImpl: public Contents::impl
  {
    static constexpr unsigned max_entry_count = 0x10000;

    friend  auto  GetRichContents(
      const mtc::span<const mtc::span<const Lexeme>>&,
      const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents;

    template <class Base, class T>
    using rebind = typename std::allocator_traits<Base>::template rebind_alloc<T>;

    struct EntryData;
    struct RichEntry;
    class  Positions;
    class  WordForms;
    using  KeyMapper = mtc::arbitrarymap<size_t, mtc::Arena::allocator<char>>;

  public:
    RichImpl(): keyMapping( allocArena.Create<KeyMapper>() )  {}

    void  AddEntry( const Lexeme&, unsigned pos );

  protected:
    mtc::Arena  allocArena;
    KeyMapper*  keyMapping;

  };

 /*
  * RichContents::EntryData
  *
  * Абстрактное представление вхождений некоторого термина.
  */
  struct RichImpl::EntryData
  {
    virtual auto  Finish() -> std::string_view = 0;
  };

 /*
  * RichContents::Positions
  *
  * Только позиции термина в документе.
  */
  class RichImpl::Positions: public EntryData
  {
    struct EntryBlock
    {
      EntryBlock* next;
      char        buff[0x200 - 0x20];
      char*       pend = buff;

      size_t  GetSpace() const {  return std::end( buff ) - pend;  }
    };

    using allocator_type = rebind<mtc::Arena::allocator<char>, EntryBlock>;

    auto  Finish() -> std::string_view override;

  public:
    Positions( const mtc::Arena::allocator<char>& alloc ): allocEntries( alloc ) {}

    void  AddRecord( unsigned entry );

  protected:
    allocator_type  allocEntries;
    EntryBlock*     entrySerials = nullptr;
    EntryBlock*     entryFilling = nullptr;
    char*           serialBuffer = nullptr;
    unsigned        entriesCount = 0;
    unsigned        serialLength = 0;
    unsigned        lastPosition = 0;
  };

 /*
  * RichContents::WordForms
  *
  * Позиции термина и формы слова с возможной компрессией при сериализации.
  */
  class RichImpl::WordForms: public EntryData
  {
    struct FormsEntry
    {
      unsigned  pos;
      uint8_t   fid;
    };
    enum: size_t
    {
      entry_size = sizeof(FormsEntry),
      block_size = 0x200,
      array_size = (block_size - 0x20) / entry_size
    };
    struct EntryBlock
    {
      EntryBlock* next;
      union
      {
        FormsEntry  buff[array_size];
        char        data[array_size * sizeof(FormsEntry)];
      };
      FormsEntry* pend = buff;
    };

    using allocator_type = rebind<mtc::Arena::allocator<char>, EntryBlock>;

    auto  Finish() -> std::string_view override;

  public:
    WordForms( const mtc::Arena::allocator<char>& alloc ): allocEntries( alloc ) {}

    void  AddRecord( unsigned pos, uint8_t fid );

  protected:
    allocator_type  allocEntries;
    EntryBlock*     entrySerials = nullptr;
    EntryBlock*     entryFilling = nullptr;
    char*           serialBuffer = nullptr;
    unsigned        entriesCount = 0;
    unsigned        serialLength = 0;
  };

  static const char dsrKey[3] = { 'd', 's', 'r' };

  // Contents implementation

  auto  Contents::get() const -> mtc::span<const EntryView>
  {
    if ( contents != nullptr )
      return contents->entryViews;
    throw std::runtime_error( "uninitialized document contents" );
  }

  // GetMiniContents implementation

  auto  GetMiniContents(
    const mtc::span<const mtc::span<const Lexeme>>& lemm,
    const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents
  {
    auto  contents = Contents();
    auto  implMini = new MiniImpl();
    auto  foundKey = decltype(implMini->keyMapping)::iterator();

  // create contents object
    contents.contents = std::shared_ptr<Contents::impl>( implMini,
      []( Contents::impl* p ){  delete (MiniImpl*)p;  } );

  // list ll words
    for ( auto& next: lemm )
      for ( auto& term: next )
      {
        if ( (foundKey = implMini->keyMapping.find( term )) == implMini->keyMapping.end() )
        {
          foundKey = implMini->keyMapping.insert(
            { term, implMini->entryViews.size() } );
          implMini->entryViews.emplace_back(
            std::string_view( (const char*)foundKey->key.data(), foundKey->key.size() ),
            std::string_view(), 0 );
        }
      }

    // set doc stats
    implMini->entryViews.emplace_back(
      std::string_view( dsrKey, sizeof(dsrKey) ),
      std::string_view( implMini->docSizeBuf, ::Serialize( implMini->docSizeBuf,
        lemm.size() ) - implMini->docSizeBuf ), 99 );

    return contents;
  }

  // GetBM25Contents implementation

  auto  GetBM25Contents( const mtc::span<const mtc::span<const Lexeme>>& lemm,
    const mtc::span<const DeliriX::MarkupTag>&, FieldHandler& ) -> Contents
  {
    auto  contents = Contents();
    auto  implBM25 = new BM25Impl();
    auto  foundKey = decltype(implBM25->keyMapping)::iterator();

  // create contents object
    contents.contents = std::shared_ptr<Contents::impl>( implBM25,
      []( Contents::impl* p ){  delete (BM25Impl*)p;  } );

  // insert and count the lexemes
    for ( auto& next: lemm )
      for ( auto& term: next )
      {
        if ( (foundKey = implBM25->keyMapping.find( term )) == implBM25->keyMapping.end() )
        {
          foundKey = implBM25->keyMapping.insert( { term, { implBM25->entryViews.size(), {} } } );

          implBM25->entryViews.emplace_back(
            std::string_view( (const char*)foundKey->key.data(), foundKey->key.size() ),
            std::string_view( foundKey->value.serial ), 1 );
        }
          else
        ++implBM25->entryViews[foundKey->value.keypos].bid;
      }

  // serialize lexeme counters
    for ( auto& next: implBM25->entryViews )
    {
      auto  length = ::Serialize( (char*)next.val.data(), next.bid ) - next.val.data();

      next.val = std::string_view( next.val.data(), length );
      next.bid = 10;
    }

  // set doc stats
    implBM25->entryViews.emplace_back(
      std::string_view( dsrKey, sizeof(dsrKey) ),
      std::string_view( implBM25->docSizeBuf, ::Serialize( implBM25->docSizeBuf,
        lemm.size() ) - implBM25->docSizeBuf ), 99 );

    return contents;
  }

  // GetRichContents implementation

  auto  GetRichContents(
    const mtc::span<const mtc::span<const Lexeme>>& lemm,
    const mtc::span<const DeliriX::MarkupTag>&      mkup, FieldHandler& fman ) -> Contents
  {
    auto    contents = Contents();
    auto    implRich = new RichImpl();
    auto    tag_pack = formats::Pack( mkup, fman );
    auto    def_opts = fman.Get( "default_field" );
    auto    no_index = std::vector<uint64_t>();
    auto    iterator = RichImpl::KeyMapper::iterator();
    size_t  ccBuffer;

  // create contents object
    contents.contents = std::shared_ptr<Contents::impl>( implRich,
      []( Contents::impl* p ){  delete (RichImpl*)p;  } );

  // set no_index flags
    if ( def_opts != nullptr && (def_opts->options & FieldOptions::ofDisableIndex) != 0 )
      mtc::bitset_set( no_index, { 0U, unsigned(lemm.size()) } );

    for ( auto& next: mkup )
    {
      auto  pfinfo = fman.Get( next.tagKey );

      if ( pfinfo != nullptr )
      {
        if ( (pfinfo->options & FieldOptions::ofDisableIndex) != 0 )
          mtc::bitset_set( no_index, { next.uLower, next.uUpper } );
        else
          mtc::bitset_del( no_index, { next.uLower, next.uUpper } );
      }
    }

  // fill lemma elements with positions
    for ( unsigned i = 0; i != lemm.size(); ++i )
    {
      if ( !no_index.empty() && mtc::bitset_get( no_index, i ) )
        continue;

      for ( auto& term: lemm[i] )
        implRich->AddEntry( term, i );
    }

  // replace all the elements with their stored arrays
    for ( auto& next: implRich->entryViews )
      next.val = ((RichImpl::EntryData*)next.val.data())->Finish();

  // store formats block
    iterator = implRich->keyMapping->insert( { Key( "fmt" ), 0 } );
    ccBuffer = tag_pack.size() + ::GetBufLen( lemm.size() );

    implRich->entryViews.emplace_back(
      std::string_view( (const char*)iterator->key.data(), iterator->key.size() ),
      std::string_view( (char*)implRich->allocArena.allocate( ccBuffer, 1 ), ccBuffer ), 99 );

    ::Serialize( ::Serialize( (char*)implRich->entryViews.back().val.data(),
      lemm.size() ), tag_pack.data(), tag_pack.size() );

    return contents;
  }

  void  RichImpl::AddEntry( const Lexeme& lex, unsigned pos )
  {
    auto  pfound = keyMapping->find( lex );

    // if the mapping is not found, create new mapping and place to value.data field
    // of entry view; else use current entry view compressor pointer
    if ( pfound == keyMapping->end() )
    {
      pfound = keyMapping->insert( { lex, entryViews.size() } );

      entryViews.emplace_back(
        std::string_view( (const char*)pfound->key.data(), pfound->key.size() ),
        std::string_view(), 0 );

      if ( lex.GetForms().empty() || lex.GetForms().front() == 0xff )
      {
        auto  pblock = allocArena.Create<Positions>();
          pblock->AddRecord( pos );
        entryViews.back().val = std::string_view( (const char*)pblock, 0 );
        entryViews.back().bid = 20;
      }
        else
      {
        auto  pblock = allocArena.Create<WordForms>();
          pblock->AddRecord( pos, lex.GetForms().front() );
        entryViews.back().val = std::string_view( (const char*)pblock, 0 );
        entryViews.back().bid = 21;
      }
    }
      else
    {
      if ( lex.GetForms().empty() || lex.GetForms().front() == 0xff )
      {
        if ( entryViews[pfound->value].bid != 20 )
          throw std::invalid_argument( "object types differ for different entries" );
        ((Positions*)entryViews[pfound->value].val.data())->AddRecord( pos );
      }
        else
      {
        if ( entryViews[pfound->value].bid != 21 )
          throw std::invalid_argument( "object types differ for different entries" );
        ((WordForms*)entryViews[pfound->value].val.data())->AddRecord( pos, lex.GetForms().front() );
      }
    }
  }

  // RichImpl::Positions implementation

  auto RichImpl::Positions::Finish() -> std::string_view
  {
  // check if no need in serialization
    if ( entrySerials->next == nullptr )
      return { entrySerials->buff, size_t(entrySerials->pend - entrySerials->buff) };

  // check if is already serialized
    if ( serialBuffer == nullptr )
    {
      auto  outputBuffer = serialBuffer = rebind<allocator_type, char>( allocEntries )
        .allocate( serialLength );

      for ( auto p = entrySerials; p != nullptr; p = p->next )
      {
        memcpy( outputBuffer, p->buff, p->pend - p->buff );
          outputBuffer += p->pend - p->buff;
      }
    }
    return { serialBuffer, size_t(serialLength) };
  }

  void  RichImpl::Positions::AddRecord( unsigned entry )
  {
    if ( entryFilling != nullptr )
    {
      if ( entriesCount < max_entry_count )
      {
        auto  storePos = entry - lastPosition - 1;
        auto  diffSize = ::GetBufLen( storePos );

        if ( diffSize > entryFilling->GetSpace() )
          entryFilling = new( entryFilling->next = allocEntries.allocate( 1 ) ) EntryBlock();

        entryFilling->pend = ::Serialize( entryFilling->pend, storePos );
          serialLength += diffSize;
        ++entriesCount;
      }
    }
      else
    {
      entryFilling = new (entrySerials = allocEntries.allocate( 1 )) EntryBlock();

      entryFilling->pend = ::Serialize( entryFilling->buff, lastPosition = entry );
        serialLength = entryFilling->pend - entryFilling->buff;
      entriesCount = 1;
    }
  }

  // RichImpl::WordForms implementation

  auto  RichImpl::WordForms::Finish() -> std::string_view
  {
    auto  pblock = entrySerials;
    auto  idform = pblock->buff->fid;
    char* output = pblock->data;
    char* outorg = output;
    bool  sameId = true;

  // check if the word form may be taken out of equation
    for ( ; sameId && pblock != nullptr; pblock = pblock->next )
      for ( auto pforms = pblock->buff; pforms != pblock->pend && sameId; ++pforms )
        sameId = pforms->fid == idform;

  // check if may be represented in existing buffer
    if ( (pblock = entrySerials)->next != nullptr )
    {
      auto  nalloc = 8 + entriesCount * (3 + (sameId ? 0 : 1));

      outorg =
      output = rebind<allocator_type, char>( allocEntries ).allocate( nalloc );
    }

  // serialize data to selected buffer
    if ( sameId )
    {
      auto  lvalue = 0x01 | (idform << 2);
      auto  pforms = pblock->buff;
      auto  oldpos = (pforms++)->pos;

      output = ::Serialize( ::Serialize( output,
        lvalue ),
        oldpos );

      for ( ;; )
      {
        for ( ; pforms != pblock->pend; oldpos = (pforms++)->pos )
          output = ::Serialize( output, pforms->pos - oldpos - 1 );

        if ( (pblock = pblock->next) != nullptr ) pforms = pblock->buff;
          else break;
      }
    }
      else
    {
      auto  pforms = pblock->buff;
      auto  oldpos = pforms->pos;

      output = ::Serialize( ::Serialize( output,
        oldpos << 2 ),
        pforms->fid );

      for ( ++pforms;; )
      {
        for ( ; pforms != pblock->pend; oldpos = (pforms++)->pos )
          output = ::Serialize( ::Serialize( output, pforms->pos - oldpos - 1 ), pforms->fid );

        if ( (pblock = pblock->next) != nullptr ) pforms = pblock->buff;
          else break;
      }
    }

    return { outorg, size_t(output - outorg) };
  }

  void  RichImpl::WordForms::AddRecord( unsigned pos, uint8_t fid )
  {
    if ( entryFilling == nullptr )
      entryFilling = new (entrySerials = allocEntries.allocate( 1 )) EntryBlock();

    if ( entriesCount < max_entry_count )
    {
      if ( entryFilling->pend == std::end(entryFilling->buff) )
        entryFilling = new( entryFilling->next = allocEntries.allocate( 1 ) ) EntryBlock();

      *entryFilling->pend++ = { pos, fid };
      ++entriesCount;
    }
  }

  auto  GetStubFields() -> FieldHandler&
  {
    return stubFields;
  }

}}
