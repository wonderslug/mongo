/** @file js_debug.cpp */

/*
 *    Copyright (C) 2010 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "js_debug.h"
#include "mongo/db/client.h"
#include <boost/thread.hpp>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include "mongo/scripting/engine.h"



namespace mongo {
    
namespace {
    void jsDebugMessageProcessingThread(Scope *scope) {
        Client::initThread( "jsDebugMessageProcessingThread" );
        fd_set set;
        int terminate = false;
        struct timeval timeout;
        
        while (!terminate) {
            // cleanout the file descriptor set.
            FD_ZERO (&set);
            
            // find the currently open file descriptors
            // so we will catch files as well as debug sockets
            // but this will just process the debug messages
            // which wont have any.
            for (int i = 3; i < FD_SETSIZE; ++i){
                errno = 0;
                if (fcntl(i, F_GETFL) < 0 && errno == EBADF)  continue;
                FD_SET(i, &set);
            }
            
            // reset the selecttimeout so we can catch any new
            // sockets that have joined
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000;
            
            // select on any read activity so that we catch any debug messages
            errno = 0;
            int activity = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
            
            // signal the scope to process messages
            scope->processDebugMessages();
            
            // error or exit?  return from the thread
            if ( activity < 0 ){
                terminate = true;
            }
        }
        return;
    }
} // namespace
    
    void startJSDebugMessageProcessingThread(Scope *scope) {
        boost::thread(jsDebugMessageProcessingThread, scope).detach();
    }
}// namespace mongo