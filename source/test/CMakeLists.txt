#########################################
# cmake file for building LCCD tests
# @author Jan Engels, DESY
#########################################

INCLUDE_DIRECTORIES( . )

# helper macro for adding tests
ADD_CUSTOM_TARGET( tests )
MACRO( ADD_LCCD_TEST _testname )

        IF( BUILD_TESTING )
            ADD_EXECUTABLE( ${_testname} ${ARGN} )
        ELSE()
            ADD_EXECUTABLE( ${_testname} EXCLUDE_FROM_ALL ${ARGN} )
        ENDIF()

        TARGET_LINK_LIBRARIES( ${_testname} lccd )

        ADD_DEPENDENCIES( tests ${_testname} )

        #INSTALL( TARGETS ${_testname} DESTINATION bin/lccd_tests )

ENDMACRO( ADD_LCCD_TEST )


# ---------------------------------------------------------------

ADD_LCCD_TEST( createcalfile ./createcalfile.cc )
ADD_LCCD_TEST( readcalfile ./readcalfile.cc ./CalibrationConstant.cc )
ADD_LCCD_TEST( readdatafile ./readdatafile.cc )
ADD_LCCD_TEST( testdbfile ./testdbfile.cc )

IF( LCCD_CONDDBMYSQL )

    ADD_LCCD_TEST( calfilefromdb ./conddbtests/calfilefromdb.cc )
    ADD_LCCD_TEST( calfiletodb ./conddbtests/calfiletodb.cc )
    ADD_LCCD_TEST( createdbfile ./conddbtests/createdbfile.cc )
    ADD_LCCD_TEST( tagdbfolder ./conddbtests/tagdbfolder.cc )
    ADD_LCCD_TEST( multiblefolders ./conddbtests/multiblefolders.cc )

ENDIF()

# ---------------------------------------------------------------

