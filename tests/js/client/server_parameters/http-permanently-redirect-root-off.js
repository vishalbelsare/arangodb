/*jshint globalstrict:false, strict:false */
/* global getOptions, assertTrue, assertFalse, assertEqual, arango */

////////////////////////////////////////////////////////////////////////////////
/// @brief test for server startup options
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB Inc, Cologne, Germany
///
/// @author Vadim Kondratyev
/// @author Copyright 2021, ArangoDB Inc, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

if (getOptions === true) {
  return {
    'http.permanently-redirect-root': "false"
  };
}
const jsunity = require('jsunity');

function testSuite() {
  return {
    testPermanentRedirectOn : function() {
      let response = arango.GET_RAW("/");

      assertTrue(response.hasOwnProperty("code"));
      assertTrue(response.hasOwnProperty("error"));
      assertTrue(response.hasOwnProperty("headers"));

      assertEqual(response.code, 302);
      assertFalse(response.error);
      assertEqual(response.headers.location, "/_db/_system/_admin/aardvark/index.html");
    },

  };
}

jsunity.run(testSuite);
return jsunity.done();
