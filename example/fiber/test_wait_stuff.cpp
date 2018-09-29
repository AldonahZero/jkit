#include <boost/fiber/all.hpp>
#include "fiber_frame_context.hpp"
#include "wait_stuff.hpp"
#include <boost/date_time.hpp>
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Verbose {
public:
    Verbose( std::string const& d):
        desc( d) {
        std::cout << boost::posix_time::second_clock::local_time() << "," << desc << " start" << std::endl;
    }

    ~Verbose() {
        std::cout << boost::posix_time::second_clock::local_time() << "," << desc << " stop" << std::endl;
    }

    Verbose( Verbose const&) = delete;
    Verbose & operator=( Verbose const&) = delete;

private:
    const std::string desc;
};

std::string sleeper( std::string const& item, int ms, bool thrw)
{
    std::ostringstream descb, funcb;
    descb << item;
    std::string desc( descb.str() );
    funcb << "  sleeper(" << item << ")" << " ms:" << ms << " thrw:" << thrw;
    Verbose v( funcb.str() );

    boost::this_fiber::sleep_for( std::chrono::milliseconds( ms) );
    if ( thrw) {
        throw std::runtime_error( desc);
    }
    return item;
}

//只要有一个任务完成就返回,不收集返回值
//禁止抛出异常
void test_wait_first_simple()
{
    using namespace std;
    using namespace boost;

    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_simple\n";
    try
    {
        wait_first_simple(
                    []() {return sleeper("wait_first_simple", 5000, false);},
        []() {return sleeper("wait_first_simple", 1000, false);});
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_simple: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_simple\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_simple_c\n";
    try
    {
        wait_first_simple_c(std::vector<std::function<string()>>{
                                []() {return sleeper("wait_first_simple_c", 5000, false);}, //这里如果true,会导致异常5秒后抛出,没有catch捕获
                                []() {return sleeper("wait_first_simple_c", 1000, false);}
                            });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_simple_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_simple_c\n";
}


//只要有一个任务完成就返回, 收集第一个返回值
//禁止抛出异常
void test_wait_first_value()
{
    using namespace std;
    using namespace boost;

    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_value\n";
    try
    {
        wait_first_value(
                    []() {return sleeper("wait_first_value", 5000, false);},
        []() {return sleeper("wait_first_value", 1000, false);}
        );
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_value: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_value\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_value_c\n";
    try
    {
        wait_first_value_c(std::vector<std::function<string()>>{
                               []() {return sleeper("wait_first_value_c", 5000, false);},
                               []() {return sleeper("wait_first_value_c", 1000, false);}
                           });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_value_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_value_c\n";
}

//只要有一个任务完成就返回, 正常返回或抛出异常
//允许抛出异常
void test_wait_first_outcome()
{
    using namespace std;
    using namespace boost;

    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_outcome\n";
    try
    {
        wait_first_outcome(
                    []() {return sleeper("wait_first_outcome", 5000, false);},
        []() {return sleeper("wait_first_outcome", 1000, false);}
        );
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_outcome: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_outcome\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_outcome_c\n";
    try
    {
        wait_first_outcome_c(std::vector<std::function<string()>>{
                                 []() {return sleeper("wait_first_outcome_c", 5000, false);},
                                 []() {return sleeper("wait_first_outcome_c", 1000, true);}
                             });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_outcome_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_outcome_c\n";
}


//收集异常直到成功返回值,如果所有任务都抛出异常，那么最终抛出异常列表，如果有一个成功返回，那么返回正常值
//允许抛出异常
void test_wait_first_success()
{
    using namespace std;
    using namespace boost;
    string res;

    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_success\n";
    try
    {
        res = wait_first_success(
                    []() {return sleeper("wait_first_success", 5000, false);},
        []() {return sleeper("wait_first_success", 1000, false);}
        );
    }
    catch(exception_list& exs)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_success, exception_list size: " << exs.size() << "\n";
    }

    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_success\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_first_success_c\n";
    try
    {
        res = wait_first_success_c(std::vector<std::function<string()>>{
                                       []() {return sleeper("wait_first_success_c", 5000, true);},
                                       []() {return sleeper("wait_first_success_c", 1000, false);}
                                   });
    }
    catch(exception_list& exs)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_first_success_c, exception_list size: " << exs.size() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_first_success_c\n";
}

//等待所有任务完成,不关心返回值
//禁止抛出异常
void test_wait_all_simple()
{
    using namespace std;
    using namespace boost;

    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_simple\n";
    try
    {
        wait_all_simple(
                    []() {return sleeper("wait_all_simple", 5000, false);},
        []() {return sleeper("wait_all_simple", 1000, false);}
        );
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_simple: " << e.what() << "\n";
    }

    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_simple\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_simple_c\n";
    try
    {
        wait_all_simple_c(std::vector<std::function<string()>>{
                              []() {return sleeper("wait_all_simple_c", 5000, false);},
                              []() {return sleeper("wait_all_simple_c", 1000, false);}
                          });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_simple_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_simple_c\n";
}

//等待所有任务完成,返回所有任务结果值
//禁止抛出异常
void test_wait_all_values()
{
    using namespace std;
    using namespace boost;

    vector<string> res;
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_values\n";
    try
    {
        res = wait_all_values(
                    []() {return sleeper("wait_all_values", 5000, false);},
        []() {return sleeper("wait_all_values", 1000, false);}
        );
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_values: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_values\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_values_c\n";
    try
    {
        res = wait_all_values_c(std::vector<std::function<string()>>{
                                    []() {return sleeper("wait_all_values_c", 5000, false);},
                                    []() {return sleeper("wait_all_values_c", 1000, false);}
                                });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_values_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_values_c\n";
}

//等待所有任务完成,直到碰到第一个异常
//允许抛出异常
void test_wait_all_until_error()
{
    using namespace std;
    using namespace boost;

    vector<string> res;
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_until_error\n";
    try
    {
        res = wait_all_until_error(
                    []() {return sleeper("wait_all_until_error", 5000, true);},
        []() {return sleeper("wait_all_until_error", 1000, false);}
        );
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "test_wait_all_until_error: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_until_error\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_until_error_c\n";
    try
    {
        res = wait_all_until_error_c(std::vector<std::function<string()>>{
                                         []() {return sleeper("wait_all_until_error_c", 5000, false);},
                                         []() {return sleeper("wait_all_until_error_c", 1000, false);}
                                     });
    }
    catch(std::exception& e)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_until_error_c: " << e.what() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_until_error_c\n";
}

//等待所有任务完成,收集所有异常,如果有至少一个异常,抛出异常
//允许抛出异常
void test_wait_all_collect_errors()
{
    using namespace std;
    using namespace boost;

    vector<string> res;
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_collect_errors\n";
    try
    {
        res = wait_all_collect_errors(
                    []() {return sleeper("wait_all_collect_errors", 5000, false);},
        []() {return sleeper("wait_all_collect_errors", 1000, false);}
        );
    }
    catch(exception_list& exs)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_collect_errors, exception_list size: " << exs.size() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_collect_errors\n";
    cout << boost::posix_time::second_clock::local_time() << "," << "before wait_all_collect_errors_c\n";
    try
    {
        res = wait_all_collect_errors_c(std::vector<std::function<string()>>{
                                            []() {return sleeper("wait_all_collect_errors_c", 5000, true);},
                                            []() {return sleeper("wait_all_collect_errors_c", 1000, false);}
                                        });
    }
    catch(exception_list& exs)
    {
        cout << boost::posix_time::second_clock::local_time() << ",exception," << "wait_all_collect_errors_c, exception_list size: " << exs.size() << "\n";
    }
    cout << boost::posix_time::second_clock::local_time() << "," << "after wait_all_until_error_c\n";
}

int main( int argc, char *argv[])
{
    FiberFrameContext& frame_cxt = FiberFrameContext::instance();
    frame_cxt.run_thread_count = 1;
    frame_cxt.init();

    std::cout << "---------------------------------------\n";
    test_wait_first_simple();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_first_value();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_first_outcome();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_first_success();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_all_simple();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_all_values();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_all_until_error();
    boost::this_fiber::sleep_for(std::chrono::seconds(8));
    std::cout << "---------------------------------------\n";
    test_wait_all_collect_errors();

    frame_cxt.wait();

    return EXIT_SUCCESS;
}
