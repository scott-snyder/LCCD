#include "lccd/DBInterface.hh"

#ifdef USE_CONDDB

// lccd headers
#include "lccd.h"
#include "lccd/StreamerMgr.hh"

// LCIO headers
#include "lcio.h"
#include "EVENT/LCIO.h"
#include "EVENT/LCCollection.h"
#include "EVENT/LCParameters.h"
#include "IO/LCWriter.h"

#include "IMPL/LCRunHeaderImpl.h"
#include "IMPL/LCEventImpl.h"
#include "UTIL/LCTime.h"

// -- CondDB headers
#include "ConditionsDB/ICondDBMgr.h"
#include "ConditionsDB/CondDBMySQLMgrFactory.h"
#include "ConditionsDB/CondDBObjFactory.h"
#include "CondDBInterface.h"



#include <memory>
#include <algorithm>
#include <functional>
#include <stdlib.h>

#include <iostream>
#include <sstream>

namespace lccd {

  /** Helper class to sort collections of conditions data w.r.t. to their validity time intervall */
  struct less_wrt_validity : public binary_function<lcio::LCCollection*,lcio::LCCollection*,bool>{
    bool operator() (lcio::LCCollection*  c0, lcio::LCCollection* c1) const {
      return ( std::atoll( c0->parameters().getStringVal("DBSince").c_str() ) <
	       std::atoll( c1->parameters().getStringVal("DBSince").c_str() ) ) ;
    }
  } ;

  DBInterface::DBInterface( const std::string& folder, bool update ) : 
    // uses init string as defined in the library: 
    // strings lib/libconddb.so | grep -E ".+:.+:.+:.+"
    // or as in $COND_DB_INIT
    _dbInit( "" ) , 
    _folder( folder ) ,
    _update( update ) ,
    _dbName("") {
    
    init() ;
  }
  
  DBInterface::DBInterface(const std::string& dbInit, const std::string& folder, bool update ) : 
    _dbInit( dbInit ) ,
    _folder( folder ),
    _update( update ) ,
    _dbName("") {
    
    init() ;
  }
  
  DBInterface::~DBInterface() {
    
    CondDBMySQLMgrFactory::destroyCondDBMgr( _condDBmgr );
 } 
  
  void DBInterface::init() {
    
    //  Database initialization
    _condDBmgr = CondDBMySQLMgrFactory::createCondDBMgr();
    _condDBmgr->init( _dbInit );
    
    if( ! _condDBmgr->isCondDBcreated() )
      
      _condDBmgr->createCondDB();
	
    // FIXME: the following calls are useless as ConDBMySQL doesn't use transactions so far...
    // is this a problem ?
    // void CondDBInterface::startUpdate() throw(CondDBException) {}
    // void CondDBInterface::startRead() throw(CondDBException) {}
    // void CondDBInterface::abort() throw(CondDBException) {}
    // void CondDBInterface::commit() throw(CondDBException) {}
    
    _condDBmgr->startRead();
    _condDBmgr->openDatabase();
    _condDBmgr->commit();
    
    _condDataAccess = _condDBmgr->getCondDBDataAccess() ;
    _condFolderMgr  = _condDBmgr->getCondDBFolderMgr() ; 
    _condTagMgr     = _condDBmgr->getCondDBTagMgr() ;
    

    // we can't get the database name through the abstract interface as this is 
    // a feature typical for MySQL, i.e. CondDBInterface* is used
    CondDBInterface* interface = dynamic_cast<CondDBInterface*>(_condDBmgr) ;
    if( interface != 0 )
      _dbName = interface->getGeneralDBname() ;
    
    std::cout << "DBInterface::init: connected to database " 
 	      << _dbName 
	      << " using folder: " << _folder
 	      << std::endl;
    

    if( _update ) {
       _condDBmgr->startUpdate();

      if (! _condFolderMgr->exist( _folder) ) {
	
 	std::cout << "DBInterface::init: folder does not exist, creating folder " << _folder << std::endl;
	
 	_condFolderMgr->createCondDBFolder( _folder, "", "", true); 
	
  	_condDBmgr->commit();
      }
//      else
//        std::cout << "CondDBFolder already exists...proceding!" << std::endl;
    }    
  }


  void DBInterface::storeCollection( LCCDTimeStamp since, LCCDTimeStamp till,
				     lcio::LCCollection* col ,
				     const std::string& description ){

    if( ! _update ) 
      throw lcio::Exception(" DBInterface::storeCollection: not in update mode !" ) ;

    lccd::VCollectionStreamer* colStr = 
      lccd::StreamerMgr::instance()->getStreamer( col->getTypeName() )  ;
    
    if( colStr == 0  )
      throw lcio::Exception( " DBInterface::storeCollection: no streamer found for collection of type " 
			     + col->getTypeName() ) ;
    
    // create auto pointer to prevent memory leaks if exceptions are thrown
    std::auto_ptr<lccd::VCollectionStreamer> colStreamer( colStr ) ;
    
    colStreamer->setCollection( col ) ;
    

    // prepend the colleciton type to the description 
    std::string dbDescription( col->getTypeName() ) ;
    dbDescription += ": " ;
    dbDescription += description ;
    
    try{
      
      ICondDBObject* condObject 
	= CondDBObjFactory::createCondDBObject(since, till, *colStreamer,
					       dbDescription  );
      
      condDataAccess()->storeCondDBObject( _folder, condObject );
      
      CondDBObjFactory::destroyCondDBObject(condObject);
      
      std::cout << " DBInterface::storeCollection: Sucessfully stored collection in database " 
		<< std::endl; 
      
      condDBMgr()->commit();
      
    } catch (CondDBException &e) {
      
      // this exception seems to never get thrown - 
      // instead it is 'caught' beforehand by some CondDBMySQL code ...
      // FIXME: should understand this
      throw lcio::Exception("DBInterface::storeCollection" +  std::string(e.getMessage()) ) ;
    }
    
  }

  lcio::LCCollection* DBInterface::findCollection( LCCDTimeStamp timeStamp, 
						   const std::string& tag ) {
    LCCDTimeStamp ignore1, ignore2 ;

    return findCollection( timeStamp, ignore1 ,ignore2 , tag ) ; 
  }


  lcio::LCCollection*  DBInterface::findCollection( LCCDTimeStamp timeStamp, 
						    LCCDTimeStamp& since, LCCDTimeStamp& till,
						    const std::string& tag ) { 
    
    ICondDBObject* condObject = 0;
    
    _condDBmgr->startRead(); // dummy method for now 
    
    CondDBKey point =  timeStamp ;
    
    condDataAccess()->findCondDBObject( condObject, _folder, point , tag );
    
    if( condObject == 0 ){
      
      std::cout << "DBInterface::findCollection: No object found in database for timestamp : " 
		<< point
		<< std::endl ;
      return 0 ;
    }
    
    since = condObject->validSince() ;
    till = condObject->validTill() ;
    
    lcio::LCCollection*  col = collectionFromCondDBObject( condObject, tag )  ;
    
    CondDBObjFactory::destroyCondDBObject(condObject);
    
    return  col ;
  }


  void DBInterface::findCollections( ColVec& colVec, const std::string& tag ){ 

    _condDBmgr->startRead(); // dummy method for now 

    ICondDBDataIterator* objIter ;

    condDataAccess()->browseObjectsInTag( objIter, _folder, tag ) ;

    ICondDBObject* condObject ; 


    // Note: due to a bug/feature in ConditionsDBMysQL implementation
    // the following runs into an endless loop:
    //  while( (condObject = objIter->current() ) != 0 ) {  
    //    ...
    //     objIter->next() ;
    //  } 

    // This way of looping through the iterator produces a memory leak,
    // as current() and next() instantiate condObjects ...
    //     do {
    //       condObject = objIter->current() ;
    //       ...
    //     } while ( objIter->next() );


    // this seems to be the only proper way of looping through the iterator:
    for( condObject = objIter->current() ; condObject != 0  ; condObject = objIter->next() ){
      
      //       cout << " ---- adding collection:  [" <<  condObject->validSince() 
      // 	   << "," << condObject->validTill() << "]" << endl ;
      
      lcio::LCCollection*  col = collectionFromCondDBObject( condObject, tag )  ;
      
      colVec.push_back( col ) ;
      
      CondDBObjFactory::destroyCondDBObject( condObject );
      
    }
    
    delete objIter ;

    // now we have to sort the array w.r.t. to the since time stamp
    // FIXME: this shouldn't be necessary according to the ConditionsDB API description ... ?
    std::sort(  colVec.begin() , colVec.end() ,   less_wrt_validity()  ) ;
    
  }


  lcio::LCCollection* DBInterface::collectionFromCondDBObject( ICondDBObject* condObject, 
							       const std::string& tag ) {
    
    
    // need to decode the collection type from object description: "LCIOTYPE: some description"
    std::string desc ;
    condObject->description( desc ) ;
    
    std::string colType( desc , 0 ,  desc.find_first_of(':') ) ;
    
    
    lccd::VCollectionStreamer* colStr = 
      lccd::StreamerMgr::instance()->getStreamer( colType )  ;
    
    if( colStr == 0  ){
      throw lcio::Exception( " DBInterface::findCollection: no streamer found for collection of type " 
 			     + colType ) ;
    }
    
    // create auto pointer to prevent memory leaks if exceptions are thrown
    std::auto_ptr<lccd::VCollectionStreamer> colStreamer( colStr ) ;
    
    condObject->data( *colStreamer ) ;
    
    LCCDTimeStamp since = condObject->validSince() ;
    LCCDTimeStamp till = condObject->validTill() ;
    
    //---- add some parameters to the collection --------
    lcio::LCCollection* col =  colStreamer->getCollection() ;
    
    
    lcio::StringVec strVec ;
    
    std::stringstream sinceStr ;
    sinceStr << since ;
    strVec.push_back(  sinceStr.str() ) ;
    strVec.push_back( lcio::LCTime( since ).getDateString() ) ;
    col->parameters().setValues( "DBSince" ,  strVec ) ;
    strVec.clear() ;

    std::stringstream tillStr ;
    tillStr << till ;
    strVec.push_back(  tillStr.str() ) ;
    strVec.push_back( lcio::LCTime( till ).getDateString() ) ;
    col->parameters().setValues( "DBTill" ,  strVec ) ;
    strVec.clear() ;

    
    std::stringstream nowStr ;
    lcio::LCTime now ;
    nowStr << now.timeStamp() ;
    strVec.push_back(  nowStr.str() ) ;
    strVec.push_back( now.getDateString() ) ;
    col->parameters().setValues( "DBQueryTime" ,  strVec ) ;
    strVec.clear() ;
    
    
    SimpleTime ins ;
    condObject->insertionTime( ins ) ; 

    std::stringstream insertStr ;
    lcio::LCTime insert(  fromSimpleTime( ins )   ) ;
    insertStr << insert.timeStamp() ;
    strVec.push_back(  insertStr.str() ) ;
    strVec.push_back( insert.getDateString() ) ;
    col->parameters().setValues( "DBInsertionTime" ,  strVec ) ;
    strVec.clear() ;
    
    
    
    std::string dbTag( tag ) ;
    if( dbTag.size() == 0 ) 
      dbTag = "HEAD" ;
    col->parameters().setValue( "DBTag" ,  dbTag ) ;
    
    
    col->parameters().setValue( "DBFolder" ,  _folder ) ;
    
    col->parameters().setValue( "DBName" ,  _dbName ) ;
    
    
    //---------------------------------------------------
    
    return col ;
  }
  

  void DBInterface::createDBFile( const std::string& tag ) {


    lcio::LCWriter* wrt = lcio::LCFactory::getInstance()->createLCWriter() ;
    

    std::string fileName( "to_do_proper_filename.slcio" ) ;   // FIXME
    
    wrt->open( fileName , lcio::LCIO::WRITE_NEW )  ;
    
    
    
    ColVec colVec ;
    findCollections( colVec, tag ) ; 


    lcio::LCRunHeader* rHdr = new lcio::LCRunHeaderImpl ;


    int evtNum(0) ;
    // add map with vailidity time intervalls and events
    for( ColVec::iterator it = colVec.begin() ; it != colVec.end() ; it++) {
      // evtNum++

      // FIXME: to be done
    }
    wrt->writeRunHeader( rHdr ) ;




    // loop over collections....
    std::string colName("ConditionsData") ; // FIXME

    evtNum = 0 ;
    for( ColVec::iterator it = colVec.begin() ; it != colVec.end() ; it++) {
      
      lcio::LCEventImpl* evt = new lcio::LCEventImpl ;
      evt->setEventNumber( evtNum++ ) ;
      
      evt->addCollection(  *it , colName  ) ;  
      
      wrt->writeEvent( evt ) ;

      delete evt ; // this deletes the collection as well
  
    }

    wrt->close() ;
    // clean up
    delete wrt ;
    delete rHdr ;
    
  }

  void DBInterface::tagFolder( const std::string& tag,  const std::string& description) {
    
    if( ! _update ) 
      throw lcio::Exception(" DBInterface::tagFolder: not in update mode !" ) ;

    condTagMgr()->createCondDBTag( tag, description ) ;
    condTagMgr()->tag( _folder , tag ) ;

  }

} ;
#endif
