# Lua support makefile
SET(LUASRC			  ${CMAKE_CURRENT_SOURCE_DIR}/lua_common.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_task.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_config.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_classifier.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_cfg_file.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_regexp.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_cdb.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_xmlrpc.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_http.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_redis.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_upstream.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_mempool.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_session.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_buffer.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_dns.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_rsa.c
					  ${CMAKE_CURRENT_SOURCE_DIR}/lua_ip.c)

SET(RSPAMD_LUA ${LUASRC} PARENT_SCOPE)