//          Copyright Nat Goodspeed + Oliver Kowalke 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "json.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <boost/fiber/all.hpp>
#include "soci/soci.h"
#include "soci/postgresql/soci-postgresql.h"
#include "soci_pgsql.hpp"
#include "fiber_frame_context.hpp"

void test_channel()
{
    using namespace std;
    using namespace boost;
    fibers::unbuffered_channel<string> ch;

    fibers::fiber f1([&ch]() {
        cout << "before push\n";
        boost::fibers::channel_op_status s = ch.push(string("test"));
        cout << "after push, status:" << static_cast<int>(s) << "\n";
    });


    fibers::fiber f2([&ch]() {
        boost::this_fiber::sleep_for(chrono::seconds(1));
        ch.close();
    });

    f2.join();
    cout << "f2 stop\n";
    f1.join();
    cout << "f1 stop\n";
}

void test_channel1()
{
    using namespace std;
    using namespace boost;
    fibers::unbuffered_channel<string> ch;

    fibers::fiber f1([&ch]() {
        cout << "before push\n";
        boost::fibers::channel_op_status s = ch.push_wait_for(string("test"), chrono::seconds(3));
        cout << "after push, status:" << static_cast<int>(s) << "\n";
    });

    fibers::fiber f2([&ch]() {
        boost::this_fiber::sleep_for(chrono::seconds(1));
        ch.close();
    });

    f2.join();
    cout << "f2 stop\n";
    f1.join();
    cout << "f1 stop\n";
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void soci_test()
{
    cout << "start soci_test" << endl;
    try
    {
        using namespace std;
        using namespace boost;
        using namespace soci;
        using namespace nlohmann;
        register_factory_postgresql();

        DBHelper db;
        json setting;
        setting["db_ip"] = "192.168.0.105";
        setting["db_port"] = 5432;
        setting["db_name"] = "oauth2";
        setting["db_user"] = "admin";
        setting["db_password"] = "88888888";
        setting["db_connect_num"] = 1;

        db.set_config(setting);
        cout << "db start" << endl;
        db.start();
        DbSessionPtr sql_ptr = db.session();
        soci::session& sql = *sql_ptr;

        string db_redirect_uri;
        string client_id = "1001";
        cout << "db start query" << endl;
        sql << "select redirect_uri from tb_party where client_id=:client_id", into(db_redirect_uri), use(client_id);
        cout << "db stop query" << endl;
        if(sql.got_data())
        {
            cout << db_redirect_uri << endl;
        }
        else
        {
            cout << "not got data" << endl;
        }
    }
    catch(std::exception& e)
    {
        cout << e.what() << endl;
    }
    cout << "stop soci_test" << endl;
}

int main( int argc, char *argv[])
{
    FiberFrameContext& frame_cxt = FiberFrameContext::instance();
    frame_cxt.run_thread_count = 1;
    frame_cxt.init();

    //test_channel();
    //test_channel1();
    soci_test();

    frame_cxt.wait();

    return EXIT_SUCCESS;
}
