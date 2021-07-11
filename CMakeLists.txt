cmake_minimum_required(VERSION 3.1)

if ( ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR} )
        message(FATAL_ERROR "In-source builds not allowed.
        Please make a new directory (called a build directory) and run CMake from there.
        You may need to remove 'CMakeCache.txt' and 'CMakeFiles/'.")
endif()

project(MobilityDB VERSION 1.0)
set(MOBILITYDB_VERSION 1.0)
include(CheckSymbolExists)

set(SRCS
src/geo_constructors.c
src/doublen.c
src/lifting.c
src/period.c
src/periodset.c
src/rangetypes_ext.c
src/skiplist.c
src/tbool_boolops.c
src/tbox.c
src/tempcache.c
src/temporal.c
src/tinstant.c
src/tinstantset.c
src/tsequence.c
src/tsequenceset.c
src/temporal_aggfuncs.c
src/temporal_analyze.c
src/temporal_boxops.c
src/temporal_compops.c
src/temporal_gist.c
src/tnumber_mathfuncs.c
src/temporal_parser.c
src/temporal_posops.c
src/temporal_selfuncs.c
src/temporal_spgist.c
src/temporal_tile.c
src/temporal_util.c
src/temporal_waggfuncs.c
src/timeops.c
src/timestampset.c
src/time_aggfuncs.c
src/time_analyze.c
src/time_gist.c
src/time_selfuncs.c
src/time_spgist.c
src/tnumber_distance.c
src/tnumber_gist.c
src/tnumber_selfuncs.c
src/tnumber_spgist.c
src/ttext_textfuncs.c
)

set(SQL
src/sql/00_catalog.in.sql
src/sql/01_period.in.sql
src/sql/03_timestampset.in.sql
src/sql/05_periodset.in.sql
src/sql/07_rangetypes_ext.in.sql
src/sql/11_timeops.in.sql
src/sql/13_time_gist.in.sql
src/sql/15_time_spgist.in.sql
src/sql/17_time_aggfuncs.in.sql
src/sql/19_geo_constructors.in.sql
src/sql/20_doublen.in.sql
src/sql/21_tbox.in.sql
src/sql/22_temporal.in.sql
src/sql/23_temporal_tile.in.sql
src/sql/24_tnumber_mathfuncs.in.sql
src/sql/26_tbool_boolops.in.sql
src/sql/27_ttext_textfuncs.in.sql
src/sql/28_temporal_compops.in.sql
src/sql/32_temporal_boxops.in.sql
src/sql/34_temporal_posops.in.sql
src/sql/36_tnumber_distance.in.sql
src/sql/38_temporal_aggfuncs.in.sql
src/sql/40_temporal_waggfuncs.in.sql
src/sql/42_temporal_gist.in.sql
src/sql/44_temporal_spgist.in.sql
src/sql/99_tempcache.in.sql
)

include(CTest)
enable_testing()

#-------------------------------------
# Verify PostgreSQL Version
#-------------------------------------
set(PG_MIN_MAJOR_VERSION "10")
set(PG_MAX_MAJOR_VERSION "14")

find_program(PGCONFIG pg_config)
if (NOT PGCONFIG)
	message(FATAL_ERROR "Could not find pg_config")
endif ()
execute_process(COMMAND ${PGCONFIG} --version OUTPUT_VARIABLE PG_VERSION_STRING OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
		COMMAND ${PGCONFIG} --version
		COMMAND sed "-es/.* //"
		COMMAND sed "s/[A-Za-z].*$//"
		COMMAND sed "s/[A-Za-z].*$//"
		COMMAND tr "." " "
		COMMAND awk "{printf \"%d%04d\", $1, $2}"
		OUTPUT_VARIABLE PG_FULL_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PGCONFIG} --version COMMAND sed "-es/.* //" "-es/\\..*//" OUTPUT_VARIABLE PG_MAJOR_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

if(${PG_MAJOR_VERSION} LESS ${PG_MIN_MAJOR_VERSION})
	message(FATAL_ERROR "Wrong PostgreSQL version: found ${PG_VERSION_STRING}, minimum required is ${PG_MIN_MAJOR_VERSION}")
elseif(${PG_MAX_MAJOR_VERSION} LESS ${PG_MAJOR_VERSION})
	message(FATAL_ERROR "Wrong PostgreSQL version: found ${PG_VERSION_STRING}, maximum supported is ${PG_MAX_MAJOR_VERSION}")
endif()
message("Found compatible ${PG_VERSION_STRING}")


set(MOBDB_DEFS -DMOBDB_PGSQL_VERSION=${PG_FULL_VERSION} -DMOBDB_PGSQL_VERSION_STR="${PG_VERSION_STRING}")
add_definitions(${MOBDB_DEFS})

execute_process(COMMAND ${PGCONFIG} --includedir --includedir-server OUTPUT_VARIABLE PostgreSQL_ACTUAL_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)

include_directories(${PostgreSQL_ACTUAL_INCLUDE_DIR})
include_directories("include")

add_definitions(-Wall -Wextra -std=gnu1x -Wno-unused-parameter)
if (CMAKE_COMPILER_IS_GNUCC)
	if (WITH_COVERAGE)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
		set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage")
	endif ()
endif ()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")

string(TOLOWER ${CMAKE_PROJECT_NAME} LCNAME)

set(CONTROLIN "control.in")
set(CONTROLOUT "${LCNAME}.control")
set(SQLPP "${LCNAME}--${MOBILITYDB_VERSION}.sql.c")
set(SQLOUT "${LCNAME}--${MOBILITYDB_VERSION}.sql")

execute_process(COMMAND ${PGCONFIG} --sharedir OUTPUT_VARIABLE PostgreSQL_SHARE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE) # FIXME: I don't think this work on Windows
execute_process(COMMAND ${PGCONFIG} --pkglibdir OUTPUT_VARIABLE PostgreSQL_EXTLIB_DIR OUTPUT_STRIP_TRAILING_WHITESPACE) # FIXME: I don't think this work on Windows

add_custom_target(sqlscript ALL DEPENDS ${CMAKE_BINARY_DIR}/${SQLOUT})
add_custom_target(control ALL DEPENDS ${CMAKE_BINARY_DIR}/${CONTROLOUT})

add_library(${CMAKE_PROJECT_NAME} MODULE ${SRCS})

if (APPLE)
	SET_TARGET_PROPERTIES(${CMAKE_PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-undefined,dynamic_lookup -bundle_loader /usr/local/bin/postgres")
endif ()

find_library(HAS_LWGEOM lwgeom)
if (HAS_LWGEOM)
	MESSAGE(STATUS "Found LWGEOM at ${HAS_LWGEOM} - compiling with PostGIS support")
	if(NOT EXISTS "${PostgreSQL_EXTLIB_DIR}/postgis-2.5.so")
		message(FATAL_ERROR "Could not find postgis-2.5.so; only PostGIS 2.5 is currently supported")
	endif()
else()
    message(FATAL_ERROR "Could not find lwgeom; ")
endif ()

include("test/scripts/test.cmake")
include("point/point.cmake")
include("npoint/npoint.cmake")

find_library(HAS_GSL gsl)
find_library(HAS_GSLCBLAS gslcblas)
if (HAS_GSL AND HAS_GSLCBLAS)
	MESSAGE(STATUS "Found GSL at ${HAS_GSL}, Found GSLCBLAS at ${HAS_GSLCBLAS}")
	target_link_libraries(${CMAKE_PROJECT_NAME} ${HAS_GSL}) # GSL)
	target_link_libraries(${CMAKE_PROJECT_NAME} ${HAS_GSLCBLAS}) # GSLCBLAS)
	include_directories(${GSL_INCLUDE_DIR})
	include_directories(${GSLCBLAS_INCLUDE_DIR})
else()
	message(FATAL_ERROR "Could not find gsl or gslcblas; ")
endif ()

find_library(HAS_LIBPROTOBUF protobuf-c)
if (HAS_LIBPROTOBUF)
	MESSAGE(STATUS "Found LIBPROTOBUF at ${HAS_LIBPROTOBUF}")
	target_link_libraries(${CMAKE_PROJECT_NAME} ${HAS_LIBPROTOBUF}) # LIBPROTOBUF)
	include_directories(${LIBPROTOBUF_INCLUDE_DIR})
else()
	message(FATAL_ERROR "Could not find protobuf-c; ")
endif ()

check_symbol_exists(ffsl "string.h" HAS_FFSL)
if(NOT HAS_FFSL)
	add_definitions(-D NO_FFSL)
endif()

add_custom_command(
	OUTPUT ${SQLPP}
	COMMAND mkdir -p ${CMAKE_BINARY_DIR}/sqlin
	COMMAND cp ${SQL} ${CMAKE_BINARY_DIR}/sqlin/
	COMMAND cd ${CMAKE_BINARY_DIR}/sqlin/ && find -type f | sort | xargs cat > ${CMAKE_BINARY_DIR}/${SQLPP}
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	DEPENDS ${SQL})

add_custom_command(
	OUTPUT ${SQLOUT}
	COMMAND "${CMAKE_C_COMPILER}" ${MOBDB_DEFS} -E ${SQLPP} | grep -v '^\#' > ${SQLOUT}
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	DEPENDS ${SQLPP})

add_custom_command(
	OUTPUT ${CONTROLOUT}
	COMMAND cat ${CONTROLIN} | sed -e's/LIBNAME/${CMAKE_PROJECT_NAME}/' -e's/VERSION/${MOBILITYDB_VERSION}/'  > ${CMAKE_BINARY_DIR}/${CONTROLOUT}
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	DEPENDS ${CONTROLIN})

install(TARGETS ${CMAKE_PROJECT_NAME} DESTINATION "${PostgreSQL_EXTLIB_DIR}")
install(FILES "${CMAKE_BINARY_DIR}/${CONTROLOUT}" DESTINATION "${PostgreSQL_SHARE_DIR}/extension")
install(FILES "${CMAKE_BINARY_DIR}/${SQLOUT}" DESTINATION "${PostgreSQL_SHARE_DIR}/extension")

add_custom_target(install_restart
	$(MAKE) install
	COMMAND systemctl restart postgresql
	DEPENDS ${PROJECT_NAME}
	COMMENT "Restarting PostgreSQL")


