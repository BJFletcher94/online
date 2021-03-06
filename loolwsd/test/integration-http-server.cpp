/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "config.h"

#include <Poco/Net/AcceptCertificateHandler.h>
#include <Poco/Net/FilePartSource.h>
#include <Poco/Net/HTMLForm.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/InvalidCertificateHandler.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/StreamCopier.h>
#include <Poco/URI.h>
#include <cppunit/extensions/HelperMacros.h>

#include <Common.hpp>
#include <Util.hpp>

#include "countloolkits.hpp"
#include "helpers.hpp"

/// Tests the HTTP GET API of loolwsd.
class HTTPServerTest : public CPPUNIT_NS::TestFixture
{
    const Poco::URI _uri;
    static int InitialLoolKitCount;

    CPPUNIT_TEST_SUITE(HTTPServerTest);

    CPPUNIT_TEST(testDiscovery);
    CPPUNIT_TEST(testLoleafletGet);
    CPPUNIT_TEST(testLoleafletPost);
    CPPUNIT_TEST(testScriptsAndLinksGet);
    CPPUNIT_TEST(testScriptsAndLinksPost);

    CPPUNIT_TEST_SUITE_END();

    void testCountHowManyLoolkits();

    void testDiscovery();
    void testLoleafletGet();
    void testLoleafletPost();
    void testScriptsAndLinksGet();
    void testScriptsAndLinksPost();

    void testNoExtraLoolKitsLeft();

public:
    HTTPServerTest()
        : _uri(helpers::getTestServerURI())
    {
#if ENABLE_SSL
        Poco::Net::initializeSSL();
        // Just accept the certificate anyway for testing purposes
        Poco::SharedPtr<Poco::Net::InvalidCertificateHandler> invalidCertHandler = new Poco::Net::AcceptCertificateHandler(false);
        Poco::Net::Context::Params sslParams;
        Poco::Net::Context::Ptr sslContext = new Poco::Net::Context(Poco::Net::Context::CLIENT_USE, sslParams);
        Poco::Net::SSLManager::instance().initializeClient(0, invalidCertHandler, sslContext);
#endif
    }

    ~HTTPServerTest()
    {
#if ENABLE_SSL
        Poco::Net::uninitializeSSL();
#endif
    }

    void setUp()
    {
        testCountHowManyLoolkits();
    }

    void tearDown()
    {
        testNoExtraLoolKitsLeft();
    }
};

int HTTPServerTest::InitialLoolKitCount = 1;

void HTTPServerTest::testCountHowManyLoolkits()
{
    InitialLoolKitCount = countLoolKitProcesses(InitialLoolKitCount);
    CPPUNIT_ASSERT(InitialLoolKitCount > 0);
}

void HTTPServerTest::testDiscovery()
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(_uri));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/hosting/discovery");
    session->sendRequest(request);

    Poco::Net::HTTPResponse response;
    session->receiveResponse(response);
    CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, response.getStatus());
    CPPUNIT_ASSERT_EQUAL(std::string("text/xml"), response.getContentType());
}

void HTTPServerTest::testLoleafletGet()
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(_uri));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/loleaflet/dist/loleaflet.html?access_token=111111111");
    Poco::Net::HTMLForm param(request);
    session->sendRequest(request);

    Poco::Net::HTTPResponse response;
    std::istream& rs = session->receiveResponse(response);
    CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, response.getStatus());
    CPPUNIT_ASSERT_EQUAL(std::string("text/html"), response.getContentType());

    std::string html;
    Poco::StreamCopier::copyToString(rs, html);

    CPPUNIT_ASSERT(html.find(param["access_token"]) != std::string::npos);
    CPPUNIT_ASSERT(html.find(_uri.getHost()) != std::string::npos);
    CPPUNIT_ASSERT(html.find(std::string(LOOLWSD_VERSION_HASH)) != std::string::npos);
}

void HTTPServerTest::testLoleafletPost()
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(_uri));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/loleaflet/dist/loleaflet.html");
    Poco::Net::HTMLForm form;
    form.set("access_token", "2222222222");
    form.prepareSubmit(request);
    std::ostream& ostr = session->sendRequest(request);
    form.write(ostr);

    Poco::Net::HTTPResponse response;
    std::istream& rs = session->receiveResponse(response);
    CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, response.getStatus());

    std::string html;
    Poco::StreamCopier::copyToString(rs, html);

    CPPUNIT_ASSERT(html.find(form["access_token"]) != std::string::npos);
    CPPUNIT_ASSERT(html.find(_uri.getHost()) != std::string::npos);
}

namespace
{

void assertHTTPFilesExist(const Poco::URI& uri, Poco::RegularExpression& expr, const std::string& html, const std::string& mimetype = std::string())
{
    Poco::RegularExpression::MatchVec matches;
    bool found = false;

    for (int offset = 0; expr.match(html, offset, matches) > 0; offset = static_cast<int>(matches[0].offset + matches[0].length))
    {
        found = true;
        CPPUNIT_ASSERT_EQUAL(2, (int)matches.size());
        Poco::URI uriScript(html.substr(matches[1].offset, matches[1].length));
        if (uriScript.getHost().empty())
        {
            std::string scriptString(uriScript.toString());

            // ignore the branding bits, it's not an error when they aren't present.
            if (scriptString.find("/branding.") != std::string::npos)
                continue;

            std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(uri));

            Poco::Net::HTTPRequest requestScript(Poco::Net::HTTPRequest::HTTP_GET, scriptString);
            session->sendRequest(requestScript);

            Poco::Net::HTTPResponse responseScript;
            session->receiveResponse(responseScript);
            CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, responseScript.getStatus());

            if (!mimetype.empty())
            CPPUNIT_ASSERT_EQUAL(mimetype, responseScript.getContentType());
        }
    }

    CPPUNIT_ASSERT_MESSAGE("No match found", found);
}

}

void HTTPServerTest::testScriptsAndLinksGet()
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(_uri));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/loleaflet/dist/loleaflet.html");
    session->sendRequest(request);

    Poco::Net::HTTPResponse response;
    std::istream& rs = session->receiveResponse(response);
    CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, response.getStatus());

    std::string html;
    Poco::StreamCopier::copyToString(rs, html);

    Poco::RegularExpression script("<script.*?src=\"(.*?)\"");
    assertHTTPFilesExist(_uri, script, html, "application/javascript");

    Poco::RegularExpression link("<link.*?href=\"(.*?)\"");
    assertHTTPFilesExist(_uri, link, html);
}

void HTTPServerTest::testScriptsAndLinksPost()
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session(helpers::createSession(_uri));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/loleaflet/dist/loleaflet.html");
    std::string body;
    request.setContentLength((int) body.length());
    session->sendRequest(request) << body;

    Poco::Net::HTTPResponse response;
    std::istream& rs = session->receiveResponse(response);
    CPPUNIT_ASSERT_EQUAL(Poco::Net::HTTPResponse::HTTP_OK, response.getStatus());

    std::string html;
    Poco::StreamCopier::copyToString(rs, html);

    Poco::RegularExpression script("<script.*?src=\"(.*?)\"");
    assertHTTPFilesExist(_uri, script, html, "application/javascript");

    Poco::RegularExpression link("<link.*?href=\"(.*?)\"");
    assertHTTPFilesExist(_uri, link, html);
}

void HTTPServerTest::testNoExtraLoolKitsLeft()
{
    const auto countNow = countLoolKitProcesses(InitialLoolKitCount);

    CPPUNIT_ASSERT_EQUAL(InitialLoolKitCount, countNow);
}

CPPUNIT_TEST_SUITE_REGISTRATION(HTTPServerTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
