/****************************************************************************
** Copyright (c) 2001-2005 quickfixengine.org  All rights reserved.
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif
#include "CallStack.h"

#ifdef HAVE_POSTGRESQL

#include "PostgreSQLStore.h"
#include "SessionID.h"
#include "SessionSettings.h"
#include "FieldConvertors.h"
#include "Parser.h"
#include "Utility.h"
#include "strptime.h"
#include <fstream>
#include <libpq-fe.h>

namespace FIX
{

const std::string PostgreSQLStoreFactory::DEFAULT_DATABASE = "quickfix";
const std::string PostgreSQLStoreFactory::DEFAULT_USER = "postgres";
const std::string PostgreSQLStoreFactory::DEFAULT_PASSWORD = "";
const std::string PostgreSQLStoreFactory::DEFAULT_HOST = "localhost";
const short PostgreSQLStoreFactory::DEFAULT_PORT = 0;

PostgreSQLStore::PostgreSQLStore
( const SessionID& s, const std::string& database, const std::string& user,
  const std::string& password, const std::string& host, short port )
  : m_sessionID( s )
{
  m_pConnection = PQsetdbLogin( host.c_str(), port == 0 ? "" : IntConvertor::convert( port ).c_str(),
                                "", "", database.c_str(), user.c_str(), password.c_str() );
  PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );

   
  if ( PQstatus( pConnection ) != CONNECTION_OK )
  {
    throw ConfigError( "Unable to connect to database" );
  }

  populateCache();
}

PostgreSQLStore::~PostgreSQLStore()
{
  PGconn* pConnection = reinterpret_cast <PGconn*>( m_pConnection );
  PQfinish( pConnection );
}

void PostgreSQLStore::populateCache()
{ QF_STACK_PUSH(PostgreSQLStore::populateCache)

  PGconn* pConnection = reinterpret_cast<PGconn*>( m_pConnection );
  std::stringstream query;

  query << "SELECT creation_time, incoming_seqnum, outgoing_seqnum FROM sessions WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "'";

  PGresult* result = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(result) != PGRES_TUPLES_OK )
    throw ConfigError( "Unable to connect to database" );

  int num_rows = PQntuples( result );
  if( num_rows > 1 )
  {
    PQclear( result );
    throw ConfigError( "Multiple entries found for session in database" );
  }

  if( num_rows == 1 )
  {
    struct tm time;
    std::string sqlTime = PQgetvalue( result, 0, 0 );
    strptime( sqlTime.c_str(), "%Y-%m-%d %H:%M:%S", &time );
    m_cache.setCreationTime (UtcTimeStamp (&time));
    m_cache.setNextTargetMsgSeqNum( atol( PQgetvalue( result, 0, 1 ) ) );
    m_cache.setNextSenderMsgSeqNum( atol( PQgetvalue( result, 0, 2 ) ) );
  }
  else
  {
    UtcTimeStamp time = m_cache.getCreationTime();
    char sqlTime[ 20 ];
    int year, month, day, hour, minute, second, millis;
    time.getYMD (year, month, day);
    time.getHMS (hour, minute, second, millis);
    sprintf (sqlTime, "%d-%02d-%02d %02d:%02d:%02d",
             year, month, day, hour, minute, second);
    std::stringstream query2;
    query2 << "INSERT INTO sessions (beginstring, sendercompid, targetcompid, session_qualifier,"
    << "creation_time, incoming_seqnum, outgoing_seqnum) VALUES("
    << "'" << m_sessionID.getBeginString().getValue() << "',"
    << "'" << m_sessionID.getSenderCompID().getValue() << "',"
    << "'" << m_sessionID.getTargetCompID().getValue() << "',"
    << "'" << m_sessionID.getSessionQualifier() << "',"
    << "'" << sqlTime << "',"
    << m_cache.getNextTargetMsgSeqNum() << ","
    << m_cache.getNextSenderMsgSeqNum() << ")";

    PGresult* result2 = PQexec( pConnection, query2.str().c_str() );
    if( PQresultStatus(result2) != PGRES_COMMAND_OK )
    {
      PQclear( result2 );
      throw ConfigError( "Unable to create session in database" );
    }
  }
  PQclear( result );

  QF_STACK_POP
}

MessageStore* PostgreSQLStoreFactory::create( const SessionID& s )
{ QF_STACK_PUSH(PostgreSQLStoreFactory::create)

  std::string database = DEFAULT_DATABASE;
  std::string user = DEFAULT_USER;
  std::string password = DEFAULT_PASSWORD;
  std::string host = DEFAULT_HOST;
  short port = DEFAULT_PORT;

  if( m_useSettings )
  {
    Dictionary settings = m_settings.get( s );

    try { database = settings.getString( POSTGRESQL_STORE_DATABASE ); }
    catch( ConfigError& ) {}

    try { user = settings.getString( POSTGRESQL_STORE_USER ); }
    catch( ConfigError& ) {}

    try { password = settings.getString( POSTGRESQL_STORE_PASSWORD ); }
    catch( ConfigError& ) {}

    try { host = settings.getString( POSTGRESQL_STORE_HOST ); }
    catch( ConfigError& ) {}

    try { port = ( short ) settings.getLong( POSTGRESQL_STORE_PORT ); }
    catch( ConfigError& ) {}
  }
  else
  {
    database = m_database;
    user = m_user;
    password = m_password;
    host = m_host;
    port = m_port;
  }

  return new PostgreSQLStore( s, database, user, password, host, port );

  QF_STACK_POP
}

void PostgreSQLStoreFactory::destroy( MessageStore* pStore )
{ QF_STACK_PUSH(PostgreSQLStoreFactory::destroy)
  delete pStore;
  QF_STACK_POP
}

bool PostgreSQLStore::set( int msgSeqNum, const std::string& msg )
throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::set)

  std::string msgCopy = msg;
  string_replace( "\"", "\\\"", msgCopy );

  PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );
  std::stringstream query;
  query << "INSERT INTO messages "
  << "(beginstring, sendercompid, targetcompid, session_qualifier, msgseqnum, message) "
  << "VALUES ("
  << "'" << m_sessionID.getBeginString().getValue() << "',"
  << "'" << m_sessionID.getSenderCompID().getValue() << "',"
  << "'" << m_sessionID.getTargetCompID().getValue() << "',"
  << "'" << m_sessionID.getSessionQualifier() << "',"
  << msgSeqNum << ","
  << "'" << msgCopy << "')";

  PGresult* result = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(result) != PGRES_COMMAND_OK )
  {
    std::stringstream query2;
    query2 << "UPDATE messages SET message='" << msg << "' WHERE "
    << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
    << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
    << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
    << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "' and "
    << "msgseqnum=" << msgSeqNum;
    PGresult* result2 = PQexec( pConnection, query2.str().c_str() );
    if( PQresultStatus(result2) != PGRES_COMMAND_OK )
    {
      PQclear( result );
      PQclear( result2 );
      throw IOException();    
    }
    PQclear( result2 );
  }
  PQclear( result );
  return true;

  QF_STACK_POP
}

void PostgreSQLStore::get( int begin, int end,
                      std::vector < std::string > & result ) const
throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::get)

  result.clear();
  PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );
  std::stringstream query;
  query << "SELECT message FROM messages WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "' and "
  << "msgseqnum>=" << begin << " and " << "msgseqnum<=" << end << " "
  << "ORDER BY msgseqnum";

  PGresult* sqlResult = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(sqlResult) != PGRES_TUPLES_OK )
  {
    PQclear( sqlResult );
    throw IOException();
  }

  int rows = PQntuples( sqlResult );
  for( int row = 0; row < rows; row++ )
    result.push_back( PQgetvalue(sqlResult, row, 0 ) );

  PQclear( sqlResult );
  QF_STACK_POP
}

int PostgreSQLStore::getNextSenderMsgSeqNum() const throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::getNextSenderMsgSeqNum)
  return m_cache.getNextSenderMsgSeqNum();
  QF_STACK_POP
}

int PostgreSQLStore::getNextTargetMsgSeqNum() const throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::getNextTargetMsgSeqNum)
  return m_cache.getNextTargetMsgSeqNum();
  QF_STACK_POP
}

void PostgreSQLStore::setNextSenderMsgSeqNum( int value ) throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::setNextSenderMsgSeqNum)

  PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );
  std::stringstream query;
  query << "UPDATE sessions SET outgoing_seqnum=" << value << " WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "'";
  PGresult* result = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(result) != PGRES_COMMAND_OK )
  {
    PQclear( result );
    throw IOException();
  }
  PQclear( result );
  m_cache.setNextSenderMsgSeqNum( value );

  QF_STACK_POP
}

void PostgreSQLStore::setNextTargetMsgSeqNum( int value ) throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::setNextTargetMsgSeqNum)

  PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );
  std::stringstream query;
  query << "UPDATE sessions SET incoming_seqnum=" << value << " WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "'";
  PGresult* result = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(result) != PGRES_COMMAND_OK )
  {
    PQclear( result );
    throw IOException();
  }
  PQclear( result );
  m_cache.setNextTargetMsgSeqNum( value );

  QF_STACK_POP
}

void PostgreSQLStore::incrNextSenderMsgSeqNum() throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::incrNextSenderMsgSeqNum)
  m_cache.incrNextSenderMsgSeqNum();
  setNextSenderMsgSeqNum( m_cache.getNextSenderMsgSeqNum() );
  QF_STACK_POP
}

void PostgreSQLStore::incrNextTargetMsgSeqNum() throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::incrNextTargetMsgSeqNum)
  m_cache.incrNextTargetMsgSeqNum();
  setNextTargetMsgSeqNum( m_cache.getNextTargetMsgSeqNum() );
  QF_STACK_POP
}

UtcTimeStamp PostgreSQLStore::getCreationTime() const throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::getCreationTime)
  return m_cache.getCreationTime();
  QF_STACK_POP
}

void PostgreSQLStore::reset() throw ( IOException )
{ QF_STACK_PUSH(PostgreSQLStore::reset)

 PGconn* pConnection = reinterpret_cast < PGconn* > ( m_pConnection );
  std::stringstream query;
  query << "DELETE FROM messages WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "'";
  PGresult* result = PQexec( pConnection, query.str().c_str() );
  if( PQresultStatus(result) != PGRES_COMMAND_OK )
  {
    PQclear( result );
    throw IOException();
  }
  PQclear( result );

  m_cache.reset();
  UtcTimeStamp time = m_cache.getCreationTime();

  int year, month, day, hour, minute, second, millis;
  time.getYMD( year, month, day );
  time.getHMS( hour, minute, second, millis );

  char sqlTime[ 20 ];
  sprintf( sqlTime, "%d-%02d-%02d %02d:%02d:%02d",
           year, month, day, hour, minute, second );

  std::stringstream query2;
  query2 << "UPDATE sessions SET creation_time='" << sqlTime << "', "
  << "incoming_seqnum=" << m_cache.getNextTargetMsgSeqNum() << ", "
  << "outgoing_seqnum=" << m_cache.getNextSenderMsgSeqNum() << " WHERE "
  << "beginstring=" << "'" << m_sessionID.getBeginString().getValue() << "' and "
  << "sendercompid=" << "'" << m_sessionID.getSenderCompID().getValue() << "' and "
  << "targetcompid=" << "'" << m_sessionID.getTargetCompID().getValue() << "' and "
  << "session_qualifier=" << "'" << m_sessionID.getSessionQualifier() << "'";
  PGresult* result2 = PQexec( pConnection, query2.str().c_str() );
  if( PQresultStatus(result2) != PGRES_COMMAND_OK )
  {
    PQclear( result2 );
    throw IOException();
  }
  PQclear( result2 );

  QF_STACK_POP
}
} //namespace FIX

#endif //HAVE_POSTGRESQL