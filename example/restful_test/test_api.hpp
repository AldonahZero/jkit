#ifndef TEST_API_H
#define TEST_API_H
#include "json.hpp"
#include "request_api_server.h"

using namespace nlohmann;
class TestApi
{
public:
    TestApi(string listen_address, int listen_port) :
        m_server(3, listen_address, listen_port)
    {
        m_server.parse_body = [](StrRequest& req, boost::any& body) ->bool {
            try
            {
                body = json::parse(req.body());
                return true;
            }
            catch (std::exception &e)
            {
                LogErrorExt << e.what();
            }
            return false;
        };

        m_server.save_body = [](StrResponse& res, boost::any& body) ->bool {
            json* body_ptr = boost::any_cast<json>(&body);
            if(nullptr == body_ptr)
                return false;
            res.body() = body_ptr->dump();
            return true;
        };

        m_server.resource["^/test_api$"][http::verb::get] = [this](RequestContext& cxt) {
            json res;
            res["error_code"] = 0;
            cxt.res_body = std::move(res);
        };
    }

  ~TestApi() = default;

  void start()
  {
      m_server.start();
  }

  void stop()
  {
      m_server.stop();
  }

private:
  RequestApiServer m_server;
};

#endif
