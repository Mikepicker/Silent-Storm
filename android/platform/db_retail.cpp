/*
 *  db_retail.cpp -- NDatabase for the retail game.db.
 *
 *  Background.  This source snapshot (January 2003) has two database back
 *  ends: ADOImport/BasicDB.cpp, which fills the typed record classes in
 *  DBFormat/ from an ADO (Access/MSSQL) connection by calling each record's
 *  Import() -- a sequence of ImportField("ColumnName", &member) -- and
 *  ADOFake/BasicDBfake.cpp, which stubs all of that and expects game.db to
 *  contain the typed records already serialised.  Every game.db in the
 *  repository, though, holds something else: for each table an object of an
 *  unregistered class (0xA1843130) that is a *generic column store* -- a dump
 *  of the ADO table itself.  The shipping engine evidently ran the ADO-style
 *  import at load time against that dump.
 *
 *  This file does the same.  It is the ADOImport driver (PreCreate every
 *  table's records by their ID column, then Import() every record with the
 *  table cursor on its row) with the retail chunk format as the "connection".
 *  The record classes' own Import() methods do the rest, unchanged.
 *
 *  Retail table object, as CStructureSaver chunks (verified on Data/game.db and
 *  Complete/game.db):
 *     2  per record: vector<int>       -- values of the int columns  (bools too)
 *     3  per record: vector<float>     -- values of the float columns
 *     4  per record: vector<wstring>   -- values of the string columns
 *     5  column descriptors: { 2: name (ANSI), 3: type }  type 0 int, 1 bool, 2 float, 3 string
 *     6  names of the int columns, in value order
 *     7  names of the float columns
 *     8  names of the string columns
 *  The top-level map is hash_map<table id, CObj<table object>>, keyed by the
 *  same table ids DBFormat registers (45 = Strings, 0xE0000001 = RPGWeaponTypes).
 */
#include "Main/StdAfx.h"
#include "ADOImport/BasicDB.h"
#include "Misc/BasicFactory.h"
#include "FileIO/BasicChunk1.h"
#include "DBFormat/DataMap.h"
#include "a5_log.h"
#include "a5_loadout.h"
#include "Main/GGeometry.h"
#include "Main/GObjectInfo.h"   /* SLoadVertex      */
#include "Main/GFileSkin.h"     /* SLoadVertexWeight */
#include "Main/GAnimFormat.h"   /* SBone             */
#include <cstdio>

#include <map>

namespace
{

/*  The generic table object.  Registered under the retail class id so the
 *  chunk serialiser instantiates it while reading game.db. */
class CRetailTable : public CObjectBase
{
    OBJECT_BASIC_METHODS( CRetailTable );
public:
    struct SColumn
    {
        std::string szName;
        int         nType;
        int operator&( CStructureSaver &f ) { f.Add( 2, &szName ); f.Add( 3, &nType ); return 0; }
    };
    std::vector< std::vector< int > >           ints;
    std::vector< std::vector< float > >         floats;
    std::vector< std::vector< std::u16string > > strings;
    std::vector< SColumn >                       columns;
    std::vector< std::string >                   intNames, floatNames, stringNames;

    int operator&( CStructureSaver &f )
    {
        f.Add( 2, &ints );
        f.Add( 3, &floats );
        f.Add( 4, &strings );
        f.Add( 5, &columns );
        f.Add( 6, &intNames );
        f.Add( 7, &floatNames );
        f.Add( 8, &stringNames );
        return 0;
    }
    int RowCount() const
    {
        size_t n = ints.size();
        if ( floats.size() > n ) n = floats.size();
        if ( strings.size() > n ) n = strings.size();
        return (int)n;
    }
};

/*  The "connection": one table's rows with a cursor, like COLETable. */
struct SCursor
{
    CRetailTable *pTable;
    int           nRow;
    std::map< std::string, std::pair< int, int > > columns;   /* name -> (kind 0/2/3, index) */

    SCursor() : pTable( 0 ), nRow( 0 ) {}
    /*  Column names in the file are raw windows-1251 bytes; the staged sources
     *  are UTF-8 (a couple of column names in DataRPG.cpp contain a Cyrillic
     *  letter -- "DamageM\u043ed" -- so this matters). */
    static std::string NameKey( const std::string &sz )
    {
        bool bAscii = true;
        for ( size_t i = 0; i < sz.size(); ++i ) if ( (unsigned char)sz[ i ] >= 0x80 ) bAscii = false;
        if ( bAscii ) return sz;
        char16_t wide[ 256 ];
        int n = MultiByteToWideChar( 1251, 0, sz.c_str(), (int)sz.size(), (LPWSTR)wide, 255 );
        std::string r;
        for ( int i = 0; i < n; ++i )
        {
            unsigned c = wide[ i ];
            if ( c < 0x80 ) r += (char)c;
            else if ( c < 0x800 ) { r += (char)( 0xC0 | ( c >> 6 ) ); r += (char)( 0x80 | ( c & 0x3F ) ); }
            else { r += (char)( 0xE0 | ( c >> 12 ) ); r += (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) ); r += (char)( 0x80 | ( c & 0x3F ) ); }
        }
        return r;
    }
    void Open( CRetailTable *p )
    {
        pTable = p; nRow = 0; columns.clear();
        if ( !p ) return;
        for ( size_t i = 0; i < p->intNames.size(); ++i )    columns[ NameKey( p->intNames[ i ] ) ]    = std::make_pair( 0, (int)i );
        for ( size_t i = 0; i < p->floatNames.size(); ++i )  columns[ NameKey( p->floatNames[ i ] ) ]  = std::make_pair( 2, (int)i );
        for ( size_t i = 0; i < p->stringNames.size(); ++i ) columns[ NameKey( p->stringNames[ i ] ) ] = std::make_pair( 3, (int)i );
    }
    bool IsEof() const { return !pTable || nRow >= pTable->RowCount(); }
    void MoveFirst() { nRow = 0; }
    void MoveNext() { ++nRow; }
    bool Find( const char *pszName, int *pnKind, int *pnIndex ) const
    {
        std::map< std::string, std::pair< int, int > >::const_iterator i = columns.find( pszName );
        if ( i == columns.end() )
            return false;
        *pnKind = i->second.first; *pnIndex = i->second.second;
        return true;
    }
    static std::string Narrow( const std::u16string &s )
    {
        std::string r;
        r.reserve( s.size() );
        for ( size_t i = 0; i < s.size(); ++i )
            r += (char)( s[ i ] < 256 ? s[ i ] : '?' );
        return r;
    }
    int GetInt( const char *pszName ) const
    {
        int k, idx;
        if ( !Find( pszName, &k, &idx ) ) return 0;
        if ( k == 0 ) return idx < (int)pTable->ints[ nRow ].size() ? pTable->ints[ nRow ][ idx ] : 0;
        if ( k == 2 ) return idx < (int)pTable->floats[ nRow ].size() ? (int)pTable->floats[ nRow ][ idx ] : 0;
        return idx < (int)pTable->strings[ nRow ].size() ? atoi( Narrow( pTable->strings[ nRow ][ idx ] ).c_str() ) : 0;
    }
    float GetFloat( const char *pszName ) const
    {
        int k, idx;
        if ( !Find( pszName, &k, &idx ) ) return 0;
        if ( k == 2 ) return idx < (int)pTable->floats[ nRow ].size() ? pTable->floats[ nRow ][ idx ] : 0;
        if ( k == 0 ) return idx < (int)pTable->ints[ nRow ].size() ? (float)pTable->ints[ nRow ][ idx ] : 0;
        return idx < (int)pTable->strings[ nRow ].size() ? (float)atof( Narrow( pTable->strings[ nRow ][ idx ] ).c_str() ) : 0;
    }
    std::u16string GetString( const char *pszName ) const
    {
        int k, idx;
        if ( !Find( pszName, &k, &idx ) ) return std::u16string();
        if ( k == 3 ) return idx < (int)pTable->strings[ nRow ].size() ? pTable->strings[ nRow ][ idx ] : std::u16string();
        char szBuf[ 64 ];
        if ( k == 0 ) snprintf( szBuf, sizeof( szBuf ), "%d", GetInt( pszName ) );
        else snprintf( szBuf, sizeof( szBuf ), "%g", GetFloat( pszName ) );
        std::u16string r;
        for ( const char *p = szBuf; *p; ++p ) r += (char16_t)*p;
        return r;
    }
    bool Has( const char *pszName ) const { int k, i; return Find( pszName, &k, &i ); }
    /*  [macos] What this table DOES carry.  A missing column used to report
     *  only the name that was asked for, which says nothing about what the
     *  retail schema calls it instead. */
    std::string ColumnNames() const
    {
        std::string r;
        for ( std::map< std::string, std::pair< int, int > >::const_iterator i = columns.begin();
              i != columns.end(); ++i )
        {
            if ( !r.empty() ) r += ", ";
            r += i->first;
        }
        return r;
    }
};

}  // namespace

REGISTER_SAVELOAD_CLASS( 0xA1843130, CRetailTable );

/* ========================================================================== */
/*  NDatabase                                                                  */
/* ========================================================================== */
namespace NDatabase
{
CClassFactory<CDBRecord>& GetRecordTypes()
{
    static CClassFactory<CDBRecord> recordTypes;
    return recordTypes;
}
typedef hash_map< int, CDBTableBase > CTablesHash;
CTablesHash& GetTables()
{
    static CTablesHash tables;
    return tables;
}
struct STableDescr { int nTableID; std::string szTable; };
static std::vector< STableDescr > &GetTableDescrs() { static std::vector< STableDescr > d; return d; }
struct SRelation
{
    std::string szTable;
    CDBTableBase *pLeft, *pRight;
    struct SElement { int nLeft, nRight; };
    std::vector< SElement > data;
};
static std::vector< SRelation > &GetRelations() { static std::vector< SRelation > r; return r; }
bool bIsDatabaseLoading = false;
static SCursor table;
static hash_map< int, CObj< CObjectBase > > retailTables;   /* what game.db held */
static std::map< std::string, std::map< std::string, int > > userNames;   /* table -> UserName -> ID */
static int nMissingColumnWarnings = 0;
}

void NDatabase::SetSource( const char * ) {}
void NDatabase::AddTable( int nTableID, const char *pszTableName, RecordCreateFunc newf )
{
    CTablesHash &tables = GetTables();
    if ( tables.find( nTableID ) != tables.end() )
        return;
    GetRecordTypes().RegisterTypeSafe( nTableID, newf );
    STableDescr t; t.nTableID = nTableID; t.szTable = pszTableName;
    GetTableDescrs().push_back( t );
    tables[ nTableID ];
}
CDBTableBase* NDatabase::GetTable( int nTableID )
{
    CTablesHash &tables = GetTables();
    CTablesHash::iterator i = tables.find( nTableID );
    return i != tables.end() ? &i->second : 0;
}
static CDBTableBase* GetTableByName( const std::string &szName )
{
    std::vector< NDatabase::STableDescr > &d = NDatabase::GetTableDescrs();
    for ( size_t i = 0; i < d.size(); ++i )
        if ( d[ i ].szTable == szName )
            return NDatabase::GetTable( d[ i ].nTableID );
    return 0;
}
void NDatabase::AddRelation( const char *pszTableName )
{
    SRelation r; r.szTable = pszTableName; r.pLeft = r.pRight = 0;
    GetRelations().push_back( r );
}
void NDatabase::Refresh( int ) {}
/*  The two import passes.  A friend of CDBTableBase, as in the ADO original. */
void NDatabase::Import()
{
    std::vector< STableDescr > &descrs = GetTableDescrs();
    int nTablesMatched = 0, nRecords = 0;
    userNames.clear();
    /* pass 1: create every table's records by ID */
    for ( size_t i = 0; i < descrs.size(); ++i )
    {
        hash_map< int, CObj< CObjectBase > >::iterator it = retailTables.find( descrs[ i ].nTableID );
        CRetailTable *pRT = it != retailTables.end() ? dynamic_cast< CRetailTable * >( it->second.GetPtr() ) : 0;
        if ( !pRT )
            continue;
        ++nTablesMatched;
        table.Open( pRT );
        if ( getenv( "A5_DB_DUMP" ) )
        {
            std::string szCols;
            for ( size_t c = 0; c < pRT->columns.size(); ++c )
            {
                if ( c ) szCols += ", ";
                szCols += pRT->columns[ c ].szName;
                szCols += "ibfs"[ pRT->columns[ c ].nType & 3 ];
            }
            a5_log( A5_PRIORITY_INFO, "game.db: table 0x%X %s: %d rows, cols %s", descrs[ i ].nTableID, descrs[ i ].szTable.c_str(), pRT->RowCount(), szCols.c_str() );
            /* A5_DB_DUMP_ROWS=<table name> also prints that table's rows */
            const char *pszRows = getenv( "A5_DB_DUMP_ROWS" );
            if ( pszRows && descrs[ i ].szTable == pszRows )
            {
                for ( table.MoveFirst(); !table.IsEof(); table.MoveNext() )
                {
                    std::string szRow;
                    for ( std::map< std::string, std::pair< int, int > >::const_iterator c = table.columns.begin(); c != table.columns.end(); ++c )
                    {
                        if ( !szRow.empty() ) szRow += " ";
                        szRow += c->first + "=";
                        if ( c->second.first == 3 )
                        {
                            std::string szVal = SCursor::Narrow( table.GetString( c->first.c_str() ) );
                            for ( size_t k = 0; k < szVal.size(); ++k )
                                if ( szVal[ k ] == '\n' || szVal[ k ] == '\r' ) szVal[ k ] = ( szVal[ k ] == '\n' ) ? '|' : ' ';
                            szRow += "\"" + szVal + "\"";
                        }
                        else if ( c->second.first == 2 ) { char b[ 32 ]; snprintf( b, 32, "%g", table.GetFloat( c->first.c_str() ) ); szRow += b; }
                        else { char b[ 32 ]; snprintf( b, 32, "%d", table.GetInt( c->first.c_str() ) ); szRow += b; }
                    }
                    a5_log( A5_PRIORITY_INFO, "  row %d: %s", table.nRow, szRow.c_str() );
                }
            }
        }
        GetTable( descrs[ i ].nTableID )->PreCreate( descrs[ i ].nTableID );
        nRecords += pRT->RowCount();
        /* the authoring names, for looking records up by name (cursor aliases) */
        if ( table.Has( "UserName" ) )
        {
            std::map< std::string, int > &names = userNames[ descrs[ i ].szTable ];
            for ( table.MoveFirst(); !table.IsEof(); table.MoveNext() )
                names.insert( std::make_pair( SCursor::Narrow( table.GetString( "UserName" ) ), table.GetInt( "ID" ) ) );
        }
    }
    /* pass 2: import fields, every table's records already existing so
     * cross-references resolve */
    for ( size_t i = 0; i < descrs.size(); ++i )
    {
        hash_map< int, CObj< CObjectBase > >::iterator it = retailTables.find( descrs[ i ].nTableID );
        CRetailTable *pRT = it != retailTables.end() ? dynamic_cast< CRetailTable * >( it->second.GetPtr() ) : 0;
        if ( !pRT )
            continue;
        table.Open( pRT );
        GetTable( descrs[ i ].nTableID )->Import();
    }
    table.Open( 0 );
    int nUnknown = 0;
    for ( hash_map< int, CObj< CObjectBase > >::iterator it = retailTables.begin(); it != retailTables.end(); ++it )
    {
        bool bKnown = false;
        for ( size_t i = 0; i < descrs.size(); ++i )
            if ( descrs[ i ].nTableID == it->first ) bKnown = true;
        if ( !bKnown ) ++nUnknown;
        if ( getenv( "A5_DB_DUMP_ALL" ) || ( !bKnown && getenv( "A5_DB_DUMP" ) ) )
        {
            CRetailTable *pRT = dynamic_cast< CRetailTable * >( it->second.GetPtr() );
            std::string szCols;
            for ( size_t c = 0; pRT && c < pRT->columns.size(); ++c )
            {
                if ( c ) szCols += ", ";
                szCols += pRT->columns[ c ].szName;
                szCols += "ibfs"[ pRT->columns[ c ].nType & 3 ];
            }
            a5_log( A5_PRIORITY_INFO, "game.db: %stable 0x%X: %d rows, cols %s", bKnown ? "" : "unknown ", it->first, pRT ? pRT->RowCount() : -1, szCols.c_str() );
        }
    }
    // [macos] A5_DB_ROWS=<hex table id> dumps that table's rows, for working out
    // what a column in a table this source does not know actually references.
    if ( const char *pszRows = getenv( "A5_DB_ROWS" ) )
    {
        int nWanted = (int)strtoul( pszRows, 0, 16 );
        hash_map< int, CObj< CObjectBase > >::iterator itR = retailTables.find( nWanted );
        CRetailTable *pRT = itR != retailTables.end() ? dynamic_cast< CRetailTable * >( itR->second.GetPtr() ) : 0;
        if ( !pRT )
            a5_log( A5_PRIORITY_INFO, "game.db: A5_DB_ROWS table 0x%X not present", nWanted );
        else
        {
            SCursor cur;
            cur.Open( pRT );
            for ( cur.MoveFirst(); !cur.IsEof(); cur.MoveNext() )
            {
                std::string szRow;
                for ( size_t c = 0; c < pRT->intNames.size(); ++c )
                {
                    char buf[ 64 ];
                    snprintf( buf, sizeof( buf ), "%s=%d ", pRT->intNames[ c ].c_str(), cur.GetInt( pRT->intNames[ c ].c_str() ) );
                    szRow += buf;
                }
                for ( size_t c = 0; c < pRT->stringNames.size(); ++c )
                    szRow += pRT->stringNames[ c ] + "='" + SCursor::Narrow( cur.GetString( pRT->stringNames[ c ].c_str() ) ) + "' ";
                a5_log( A5_PRIORITY_INFO, "game.db: ROW 0x%X: %s", nWanted, szRow.c_str() );
            }
        }
    }
    a5_log( A5_PRIORITY_INFO, "game.db: retail format, %d/%d tables matched DBFormat (%d records imported, %d tables in the file unknown to this source, %d missing-column warnings)",
            nTablesMatched, (int)descrs.size(), nRecords, nUnknown, nMissingColumnWarnings );
}

/* ---- field access from the record classes' Import() ----------------------- */
static void MissingColumn( const char *pszName )
{
    /* once per table and column, not per row */
    static std::map< std::string, int > seen;
    if ( NDatabase::table.pTable == 0 ) seen.clear();
    std::string szKey = pszName;
    if ( seen[ szKey ] == 0 || seen[ szKey ] != (int)(size_t)NDatabase::table.pTable )
    {
        seen[ szKey ] = (int)(size_t)NDatabase::table.pTable;
        ++NDatabase::nMissingColumnWarnings;
        a5_log( A5_PRIORITY_WARN, "game.db: column '%s' is not in the table (%d rows) - the record field stays default", pszName, NDatabase::table.pTable ? NDatabase::table.pTable->RowCount() : 0 );
        {   /* [macos] once per table, say what it does carry */
            static std::map< const void *, int > listed;
            const void *pKey = (const void *)NDatabase::table.pTable;
            if ( pKey && listed.find( pKey ) == listed.end() )
            {
                listed[ pKey ] = 1;
                a5_log( A5_PRIORITY_WARN, "game.db:   that table has: %s",
                        NDatabase::table.ColumnNames().c_str() );
            }
        }
    }
}
void NDatabase::ImportField( const char *pszFieldName, int *pData )
{
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); *pData = 0; return; }
    *pData = table.GetInt( pszFieldName );
}
void NDatabase::ImportField( const char *pszFieldName, bool *pData )
{
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); *pData = false; return; }
    *pData = table.GetInt( pszFieldName ) != 0;
}
void NDatabase::ImportField( const char *pszFieldName, float *pData )
{
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); *pData = 0; return; }
    *pData = table.GetFloat( pszFieldName );
}
void NDatabase::ImportField( const char *pszFieldName, std::string *pData )
{
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); pData->clear(); return; }
    *pData = SCursor::Narrow( table.GetString( pszFieldName ) );
}
void NDatabase::ImportField( const char *pszFieldName, std::u16string *pData )
{
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); pData->clear(); return; }
    *pData = table.GetString( pszFieldName );
}
void NDatabase::ImportField( const char *pszFieldName, CDBRecord **pRef, CDBTableBase *pDestTable )
{
    *pRef = 0;
    if ( !table.Has( pszFieldName ) ) { MissingColumn( pszFieldName ); return; }
    const int nID = table.GetInt( pszFieldName );
    if ( pDestTable && nID > 0 )
        *pRef = pDestTable->GetDBRecord( nID );
    if ( getenv( "A5_DB_TRACE_REF" ) && strcmp( pszFieldName, getenv( "A5_DB_TRACE_REF" ) ) == 0 )
        a5_log( A5_PRIORITY_INFO, "game.db: ref %s = %d -> table %p record %p (row %d)", pszFieldName, nID, (void*)pDestTable, (void*)*pRef, table.nRow );
}
void NDatabase::ImportRelation( CDBRecord *pSrc, CDBTableBase *pDestTable, std::vector< CPtr<CDBRecord> > *pRefs )
{
    /* Relations (RPGPers2Scripts) are not among the tables in the retail
     * game.db as shipped; the list stays empty and the engine copes (units
     * simply have no scripts attached from the database). */
    pRefs->clear();
    (void)pSrc; (void)pDestTable;
}

/* ---- the load ---------------------------------------------------------------- */
using namespace NDatabase;
void CDBTableBase::PreCreate( int nTypeID )
{
    records.clear();
    for ( table.MoveFirst(); !table.IsEof(); table.MoveNext() )
    {
        CDBRecord *pRes = GetRecordTypes().CreateObject( nTypeID );
        if ( !pRes )
            break;
        pRes->nID = table.GetInt( "ID" );
        records[ pRes->nID ] = pRes;
    }
}
void CDBTableBase::Refresh( int nTypeID ) { PreCreate( nTypeID ); }
void CDBTableBase::Import()
{
    for ( table.MoveFirst(); !table.IsEof(); table.MoveNext() )
    {
        const int nID = table.GetInt( "ID" );
        CRecordHash::iterator i = records.find( nID );
        if ( i == records.end() )
            continue;
        i->second->Import();
    }
}
CDBRecord* CDBTableBase::GetDBRecord( int nID )
{
    CRecordHash::iterator i = records.find( nID );
    return i == records.end() ? 0 : i->second.GetPtr();
}

/* ========================================================================== */
/*  Enemy loadout templates -- see a5_loadout.h for the chain and why it is    */
/*  captured here rather than imported as records.                             */
/* ========================================================================== */
namespace {
struct SLoadoutBundle { int nChestID; float fWeight; int nLevel; };
struct SChestEntry    { int nItemID; int nQuantity; int nMaxQuantity; };
struct SPersTemplates { int nInHand, nInBackpack; };

const int TBL_TEMPLATES = 0x6A;   /* id -> UserName ("handRifles", ...)        */
const int TBL_BUNDLES   = 0x6C;   /* id(=ChestID), TemplateID, RndWeight, Level */
const int TBL_CHESTS    = 0x6B;   /* ChestID -> RPGItemID, Quantity, MaxQuantity*/
const int TBL_PERS      = 0xE0000004;

std::map< int, std::vector< SLoadoutBundle > > g_bundlesByTemplate;
std::map< int, std::vector< SChestEntry > >    g_entriesByChest;
std::map< int, SPersTemplates >                g_templatesByPers;

CRetailTable *FindRaw( int nTableID )
{
    hash_map< int, CObj< CObjectBase > >::iterator it = retailTables.find( nTableID );
    return it != retailTables.end() ? dynamic_cast< CRetailTable * >( it->second.GetPtr() ) : 0;
}

void CaptureLoadoutTables()
{
    g_bundlesByTemplate.clear();
    g_entriesByChest.clear();
    g_templatesByPers.clear();

    SCursor cur;
    if ( CRetailTable *pT = FindRaw( TBL_BUNDLES ) )
    {
        cur.Open( pT );
        for ( cur.MoveFirst(); !cur.IsEof(); cur.MoveNext() )
        {
            SLoadoutBundle b;
            b.nChestID = cur.GetInt( "ID" );
            b.fWeight  = cur.GetFloat( "RndWeight" );
            b.nLevel   = cur.GetInt( "Level" );
            if ( b.fWeight <= 0 ) b.fWeight = 1.0f;
            g_bundlesByTemplate[ cur.GetInt( "TemplateID" ) ].push_back( b );
        }
    }
    if ( CRetailTable *pT = FindRaw( TBL_CHESTS ) )
    {
        cur.Open( pT );
        for ( cur.MoveFirst(); !cur.IsEof(); cur.MoveNext() )
        {
            SChestEntry e;
            e.nItemID       = cur.GetInt( "RPGItemID" );
            e.nQuantity     = cur.GetInt( "Quantity" );
            e.nMaxQuantity  = cur.GetInt( "MaxQuantity" );
            if ( e.nItemID > 0 )
                g_entriesByChest[ cur.GetInt( "ChestID" ) ].push_back( e );
        }
    }
    if ( CRetailTable *pT = FindRaw( TBL_PERS ) )
    {
        cur.Open( pT );
        for ( cur.MoveFirst(); !cur.IsEof(); cur.MoveNext() )
        {
            SPersTemplates t;
            t.nInHand     = cur.GetInt( "WeaponInHand" );
            t.nInBackpack = cur.GetInt( "WeaponInBackpack" );
            if ( t.nInHand > 0 || t.nInBackpack > 0 )
                g_templatesByPers[ cur.GetInt( "ID" ) ] = t;
        }
    }
    int nTemplates = 0;
    if ( CRetailTable *pT = FindRaw( TBL_TEMPLATES ) ) nTemplates = pT->RowCount();
    a5_log( A5_PRIORITY_INFO,
            "game.db: loadouts captured -- %d templates, %d with bundles, %d chests, %d persons",
            nTemplates, (int)g_bundlesByTemplate.size(), (int)g_entriesByChest.size(),
            (int)g_templatesByPers.size() );
    if ( getenv( "A5_LOADOUT_TEST" ) )
    {
        for ( std::map< int, SPersTemplates >::const_iterator p = g_templatesByPers.begin();
              p != g_templatesByPers.end(); ++p )
        {
            for ( int nLvl = 1; nLvl <= 12; nLvl += 11 )
            {
                SA5LoadoutItem items[ 16 ];
                int n = a5_loadout_pick( p->first, nLvl, 1234, items, 16 );
                std::string szList;
                for ( int i = 0; i < n; ++i )
                {
                    char buf[ 64 ];
                    snprintf( buf, sizeof( buf ), "item%d x%d ", items[ i ].nItemID, items[ i ].nCount );
                    szList += buf;
                }
                a5_log( A5_PRIORITY_INFO, "loadout-test: pers=%d lvl=%d hand=%d pack=%d -> %s",
                        p->first, nLvl, p->second.nInHand, p->second.nInBackpack,
                        n ? szList.c_str() : "<nothing>" );
            }
        }
    }
}

/*  Highest level band at or below nLevel; weighted pick among that band.
 *  Falls back to the lowest band when the unit is below every entry.        */
const SLoadoutBundle *PickBundle( int nTemplateID, int nLevel, unsigned nSeed )
{
    std::map< int, std::vector< SLoadoutBundle > >::const_iterator it =
        g_bundlesByTemplate.find( nTemplateID );
    if ( it == g_bundlesByTemplate.end() || it->second.empty() )
        return 0;
    const std::vector< SLoadoutBundle > &all = it->second;
    int nBand = -1;
    for ( size_t i = 0; i < all.size(); ++i )
        if ( all[ i ].nLevel <= nLevel && all[ i ].nLevel > nBand )
            nBand = all[ i ].nLevel;
    if ( nBand < 0 )
    {
        nBand = all[ 0 ].nLevel;
        for ( size_t i = 1; i < all.size(); ++i )
            if ( all[ i ].nLevel < nBand ) nBand = all[ i ].nLevel;
    }
    float fTotal = 0;
    for ( size_t i = 0; i < all.size(); ++i )
        if ( all[ i ].nLevel == nBand ) fTotal += all[ i ].fWeight;
    if ( fTotal <= 0 )
        return 0;
    /*  A cheap deterministic hash so a reloaded save re-arms identically.   */
    unsigned h = nSeed * 1664525u + 1013904223u;
    h ^= h >> 16;
    float fPick = ( h % 100000u ) / 100000.0f * fTotal;
    for ( size_t i = 0; i < all.size(); ++i )
    {
        if ( all[ i ].nLevel != nBand ) continue;
        fPick -= all[ i ].fWeight;
        if ( fPick <= 0 ) return &all[ i ];
    }
    for ( size_t i = 0; i < all.size(); ++i )
        if ( all[ i ].nLevel == nBand ) return &all[ i ];
    return 0;
}

int EmitChest( int nChestID, unsigned nSeed, SA5LoadoutItem *pOut, int nMax, int nAt )
{
    std::map< int, std::vector< SChestEntry > >::const_iterator it =
        g_entriesByChest.find( nChestID );
    if ( it == g_entriesByChest.end() )
        return nAt;
    for ( size_t i = 0; i < it->second.size() && nAt < nMax; ++i )
    {
        const SChestEntry &e = it->second[ i ];
        int nLo = e.nQuantity, nHi = e.nMaxQuantity > nLo ? e.nMaxQuantity : nLo;
        int nCount = nLo;
        if ( nHi > nLo )
        {
            unsigned h = ( nSeed + (unsigned)i * 2654435761u ) * 1103515245u + 12345u;
            nCount = nLo + (int)( ( h >> 16 ) % (unsigned)( nHi - nLo + 1 ) );
        }
        if ( nCount <= 0 )
            continue;
        pOut[ nAt ].nItemID = e.nItemID;
        pOut[ nAt ].nCount  = nCount;
        ++nAt;
    }
    return nAt;
}
}   /* anonymous namespace */

bool a5_loadout_available()
{
    return !g_bundlesByTemplate.empty() && !g_entriesByChest.empty() && !g_templatesByPers.empty();
}

int a5_loadout_pick( int nPersID, int nLevel, int nSeed, SA5LoadoutItem *pOut, int nMax )
{
    std::map< int, SPersTemplates >::const_iterator it = g_templatesByPers.find( nPersID );
    if ( it == g_templatesByPers.end() || !pOut || nMax <= 0 )
        return 0;
    int nAt = 0;
    const int anTemplates[ 2 ] = { it->second.nInHand, it->second.nInBackpack };
    for ( int t = 0; t < 2; ++t )
    {
        if ( anTemplates[ t ] <= 0 )
            continue;
        unsigned nMix = (unsigned)nSeed * 31u + (unsigned)nPersID * 7u + (unsigned)t;
        if ( const SLoadoutBundle *pB = PickBundle( anTemplates[ t ], nLevel, nMix ) )
            nAt = EmitChest( pB->nChestID, nMix, pOut, nMax, nAt );
    }
    return nAt;
}

/* ========================================================================== */
/*  Skeleton and bind-pose export.  CFileSkeleton::Recalc reads the Skeletons
 *  resource as chunk 1 = vector<SBone> (name, parent index, rest pos/rot/
 *  scale) and chunk 2 = a scale flag; CFileBind::Recalc reads the Binds
 *  resource, keyed by GEOMETRY id rather than skeleton id, as chunk 4 =
 *  vector<SHMatrix>, the inverse bind matrices.  Together with the per-vertex
 *  weights that is a complete rig.                                          */
extern "C" int a5_export_skeleton( const char *pszPath, const char *pszOut )
{
    std::vector< NAnimation::SBone > bones;
    bool bScale = false;
    try
    {
        CFileStream in;
        in.OpenRead( pszPath );
        CStructureSaver saver( in, CStructureSaver::READ );
        saver.Add( 1, &bones );
        saver.Add( 2, &bScale );
    }
    catch ( ... )
    {
        a5_log( A5_PRIORITY_ERROR, "export: cannot read skeleton %s", pszPath );
        return 1;
    }
    FILE *pf = fopen( pszOut, "w" );
    if ( !pf )
        return 1;
    fprintf( pf, "# index parent name posX posY posZ quatX quatY quatZ quatW scaleX scaleY scaleZ\n" );
    fprintf( pf, "# %d bones, scaled=%d\n", (int)bones.size(), (int)bScale );
    for ( size_t i = 0; i < bones.size(); ++i )
    {
        const NAnimation::SBone &b = bones[i];
        /*  CQuat keeps x/y/z/w in a private union; the layout is four floats. */
        const float *pQ = reinterpret_cast< const float * >( &b.rot );
        fprintf( pf, "%d %d %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                 (int)i, b.nParent, b.szName.empty() ? "unnamed" : b.szName.c_str(),
                 b.pos.x, b.pos.y, b.pos.z,
                 pQ[0], pQ[1], pQ[2], pQ[3],
                 b.scale.x, b.scale.y, b.scale.z );
    }
    fclose( pf );
    a5_log( A5_PRIORITY_INFO, "export: %s -> %s (%d bones)", pszPath, pszOut, (int)bones.size() );
    return 0;
}

extern "C" int a5_export_binds( const char *pszPath, const char *pszOut )
{
    std::vector< SHMatrix > inv;
    try
    {
        CFileStream in;
        in.OpenRead( pszPath );
        CStructureSaver saver( in, CStructureSaver::READ );
        saver.Add( 4, &inv );
    }
    catch ( ... )
    {
        a5_log( A5_PRIORITY_ERROR, "export: cannot read binds %s", pszPath );
        return 1;
    }
    FILE *pf = fopen( pszOut, "w" );
    if ( !pf )
        return 1;
    fprintf( pf, "# inverse bind matrices, 16 floats per bone, row major\n# %d bones\n", (int)inv.size() );
    for ( size_t i = 0; i < inv.size(); ++i )
    {
        const float *m = &inv[i]._11;
        for ( int k = 0; k < 16; ++k )
            fprintf( pf, "%s%.6f", k ? " " : "", m[k] );
        fprintf( pf, "\n" );
    }
    fclose( pf );
    a5_log( A5_PRIORITY_INFO, "export: %s -> %s (%d matrices)", pszPath, pszOut, (int)inv.size() );
    return 0;
}

/* ========================================================================== */
/*  Geometry export.  A character's mesh is not in game.db -- the database only
 *  holds its bounds -- it is a file in the Geometries resource, keyed by the
 *  CGeometry record id.  CObjectInfoLoader::RecalcValue reads it as four
 *  chunks, and this is the same read, written back out as Wavefront OBJ:
 *
 *      1 : vector<SLoadVertex>        pos, normal, uv, tangent basis, colour
 *      2 : vector<WORD> indices       polygon corner indices
 *      3 : vector<WORD> polys         one-past-the-end offset per polygon
 *      5 : vector<SLoadVertexWeight>  { weight, vertex, bone } skinning triples
 *
 *  Polygons are emitted as-is rather than triangulated, so the original
 *  topology survives into Blender.  Weights are written beside the OBJ as a
 *  plain text table, since OBJ cannot carry them.                            */
extern "C" int a5_export_geometry( const char *pszPath, const char *pszOutBase )
{
    std::vector< NGScene::SLoadVertex > verts;
    std::vector< WORD > indices, polys;
    std::vector< NGScene::SLoadVertexWeight > wghts;
    try
    {
        CFileStream in;
        in.OpenRead( pszPath );
        CStructureSaver saver( in, CStructureSaver::READ );
        saver.Add( 1, &verts );
        saver.Add( 2, &indices );
        saver.Add( 3, &polys );
        saver.Add( 5, &wghts );
    }
    catch ( ... )
    {
        a5_log( A5_PRIORITY_ERROR, "export: cannot read geometry %s", pszPath );
        return 1;
    }
    if ( verts.empty() || polys.size() < 2 )
    {
        a5_log( A5_PRIORITY_ERROR, "export: %s has %d verts / %d polys -- nothing to write",
                pszPath, (int)verts.size(), (int)polys.size() );
        return 1;
    }

    std::string szObj = std::string( pszOutBase ) + ".obj";
    FILE *pf = fopen( szObj.c_str(), "w" );
    if ( !pf )
        return 1;
    fprintf( pf, "# Silent Storm geometry %s\n# %d vertices, %d polygons\n",
             pszPath, (int)verts.size(), (int)polys.size() - 1 );
    for ( size_t i = 0; i < verts.size(); ++i )
        fprintf( pf, "v %.6f %.6f %.6f\n", verts[i].pos.x, verts[i].pos.y, verts[i].pos.z );
    for ( size_t i = 0; i < verts.size(); ++i )
        fprintf( pf, "vn %.6f %.6f %.6f\n", verts[i].normal.x, verts[i].normal.y, verts[i].normal.z );
    for ( size_t i = 0; i < verts.size(); ++i )
        fprintf( pf, "vt %.6f %.6f\n", verts[i].tex.x, 1.0f - verts[i].tex.y );
    fprintf( pf, "o geometry\n" );
    int nFaces = 0;
    for ( size_t k = 1; k < polys.size(); ++k )
    {
        if ( polys[k] <= polys[k-1] || polys[k] > indices.size() )
            continue;
        fprintf( pf, "f" );
        for ( int i = polys[k-1]; i < polys[k]; ++i )
        {
            const int v = indices[i] + 1;      /* OBJ is 1-based */
            fprintf( pf, " %d/%d/%d", v, v, v );
        }
        fprintf( pf, "\n" );
        ++nFaces;
    }
    fclose( pf );

    int nBones = 0;
    if ( !wghts.empty() )
    {
        std::string szW = std::string( pszOutBase ) + ".weights.txt";
        if ( FILE *pw = fopen( szW.c_str(), "w" ) )
        {
            fprintf( pw, "# vertex bone weight   (%d entries)\n", (int)wghts.size() );
            for ( size_t i = 0; i < wghts.size(); ++i )
            {
                fprintf( pw, "%d %d %.6f\n", wghts[i].nVertex, wghts[i].nBone, wghts[i].fWeight );
                if ( wghts[i].nBone + 1 > nBones ) nBones = wghts[i].nBone + 1;
            }
            fclose( pw );
        }
    }
    a5_log( A5_PRIORITY_INFO, "export: %s -> %s  (%d verts, %d faces, %d weights over %d bones)",
            pszPath, szObj.c_str(), (int)verts.size(), nFaces, (int)wghts.size(), nBones );
    return 0;
}

void NDatabase::Serialize( CDataStream &file, CStructureSaver::EMode mode )
{
    if ( mode != CStructureSaver::READ )
    {
        a5_log( A5_PRIORITY_WARN, "game.db: writing is not supported by the retail loader" );
        return;
    }
    bIsDatabaseLoading = true;
    retailTables.clear();
    {
        CStructureSaver f( file, mode );
        f.Add( 1, &retailTables );
    }
    CaptureLoadoutTables();   /* before Import(): needs the raw tables */
    Import();
    /*  The shipping game builds the cross-record links right after importing
     *  (Soft/Andy/May03/Main.cpp: NDatabase::Import( true ) then
     *  NDb::BuildMapLinks( translation )).  This snapshot's own Main.cpp does
     *  not, because at that point game.db was written by DataImport with the
     *  links already serialised into the records; the retail game.db is the
     *  generic column dump, so they have to be rebuilt here or they are simply
     *  absent.  Without this, CSkeleton::pAnimations is empty for every
     *  skeleton -- units are created with no animation at all and the first
     *  CUnitAnimator::StandStill dereferences the null it gets back -- and the
     *  debris materials, item-model-to-uniform and grenade tables are empty
     *  too. */
    NDb::BuildMapLinks();
    retailTables.clear();
    bIsDatabaseLoading = false;
}

/* ========================================================================== */
/*  Lookups by authoring name, and the UI-texture alias table                   */
/* ========================================================================== */
int NDatabase::FindRecordByUserName( const char *pszTable, const char *pszUserName )
{
    std::map< std::string, std::map< std::string, int > >::const_iterator t = userNames.find( pszTable );
    if ( t == userNames.end() )
        return -1;
    std::map< std::string, int >::const_iterator r = t->second.find( pszUserName );
    return r == t->second.end() ? -1 : r->second;
}

/*  This source hard-codes UITexture ids for the cursors (Interface.cpp,
 *  iGameStates.h); the retail table renumbered them (202 is "HitLocation -
 *  Head" there, "Normal" is 292).  Map by the authoring name. */
int NDatabase::AliasUITexture( int nID )
{
    struct SAlias { int nID; const char *pszName; };
    static const SAlias ALIASES[] = {
        { 202, "Normal" },                 /* N_DEFAULT_CURSOR / N_CURSOR_MOVE / ROTATE */
        { 281, "Normal" },                 /* N_CURSOR_NORMAL */
        { 217, "Busy" },
        { 218, "Blocked" },
        { 208, "Heal" },
        { 203, "Attack - FireArm" },       /* N_CURSOR_ATTACK */
        { 204, "Attack - ColdSteel" },     /* melee */
        { 210, "Attack - FireArm" },       /* rifle */
        { 209, "Attack - FireArm" },       /* pistol */
        { 205, "Attack - FireArm" },       /* machine gun */
        { 206, "Attack - Grenade" },
        { 282, "HitLocation - Head" },
        { 283, "HitLocation - Body" },
        { 286, "HitLocation - Left Arm" },
        { 287, "HitLocation - Right Arm" },
        { 284, "HitLocation - Left Leg" },
        { 285, "HitLocation - Right Leg" },
        { 207, "Use" },                    /* open/close */
        { 579, "Unload" },
        { 492, "Normal Pen" },             /* the options / chapter-map cursor */
    };
    static std::map< int, int > resolved;
    std::map< int, int >::const_iterator r = resolved.find( nID );
    if ( r != resolved.end() )
        return r->second;
    int nResult = nID;
    for ( size_t i = 0; i < sizeof( ALIASES ) / sizeof( ALIASES[ 0 ] ); ++i )
        if ( ALIASES[ i ].nID == nID )
        {
            const int nFound = FindRecordByUserName( "UITextures", ALIASES[ i ].pszName );
            if ( nFound > 0 )
                nResult = nFound;
            break;
        }
    resolved[ nID ] = nResult;
    return nResult;
}
