/// \file Demonstration program for the assertion bug in Boost.Beast
///
/// \author
/// Tobias Kux <kux@smart-hmi.de>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <iostream>

#if !defined(WITHOUT_TLS)
#  define WITHOUT_TLS 0
#endif // !defined(WITHOUT_TLS)
#if !defined(ASIO_STREAMS)
#  define ASIO_STREAMS 0
#endif // !defined(ASIO_STREAMS)

#define TEST_CASE_HOSTNAME "localhost"
#define TEST_CASE_SERVICE "8899"
#define TEST_KEY \
"-----BEGIN EC PARAMETERS-----" "\n"\
"BggqhkjOPQMBBw==" "\n" \
"-----END EC PARAMETERS-----" "\n" \
"-----BEGIN EC PRIVATE KEY-----" "\n" \
"MHcCAQEEIENhHe8T7GI4cDbzcHc9jLkPPWAnwA9AYfG76hiR4xhqoAoGCCqGSM49" "\n" \
"AwEHoUQDQgAECqCBeItxyEC58vEZDrO+l7cvMkVY7HBD5oof8/8TfQsdSC26znaT" "\n" \
"WOCeNDj0CffLCghsl+BKNM+jILx6jloFIg==" "\n" \
"-----END EC PRIVATE KEY-----"
#define TEST_CERT \
"-----BEGIN CERTIFICATE-----" "\n" \
"MIIBvTCCAWOgAwIBAgIUGYi5NtfnF2c6iYEhZHhY/+GXndcwCgYIKoZIzj0EAwIw" "\n" \
"NDELMAkGA1UEBhMCREUxDDAKBgNVBAgMA05SVzEXMBUGA1UECgwOU21hcnQgSE1J" "\n" \
"IEdtYkgwHhcNMjIwMTE5MDkzMzM0WhcNMjMwMTE5MDkzMzM0WjA0MQswCQYDVQQG" "\n" \
"EwJERTEMMAoGA1UECAwDTlJXMRcwFQYDVQQKDA5TbWFydCBITUkgR21iSDBZMBMG" "\n" \
"ByqGSM49AgEGCCqGSM49AwEHA0IABAqggXiLcchAufLxGQ6zvpe3LzJFWOxwQ+aK" "\n" \
"H/P/E30LHUgtus52k1jgnjQ49An3ywoIbJfgSjTPoyC8eo5aBSKjUzBRMB0GA1Ud" "\n" \
"DgQWBBRzY8GivNBEtiWWmpgFMcztks+ymDAfBgNVHSMEGDAWgBRzY8GivNBEtiWW" "\n" \
"mpgFMcztks+ymDAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0gAMEUCIQDe" "\n" \
"gLJOJlDEFeE04MdBxPnf+dMx/DoclkAFGQSzQ5yJdQIgXDLnUmdrvTmYE014tWyl" "\n" \
"HiOqjizpYqi4QAFVYCUv3NI=" "\n" \
"-----END CERTIFICATE-----"

#if ASIO_STREAMS
using tcp_layer = boost::asio::ip::tcp::socket;
using ssl_layer = boost::asio::ssl::stream<tcp_layer>;
#else
using tcp_layer = boost::beast::tcp_stream;
using ssl_layer = boost::beast::ssl_stream<boost::beast::tcp_stream>;
#endif

/// \brief Websocket test client
/// Connects to host TEST_CASE_HOSTNAME on port TEST_CASE_SERVICE and
/// continually sends (and reads) data.
class ws_test_client {
public:
	using executor_type = boost::asio::any_io_executor;
#if WITHOUT_TLS
	using stream_type = boost::beast::websocket::stream<tcp_layer>;
#else
	using stream_type = boost::beast::websocket::stream<ssl_layer>;
#endif // WITHOUT_TLS

public:
	ws_test_client(executor_type executor)
		: executor_{ std::move(executor) }
	{
	}

	/// Returns the associated executor.
	[[nodiscard]] executor_type executor() const noexcept {
		return executor_;
	}

	/// Returns a reference to the underlying stream.
	[[nodiscard]] stream_type & stream() noexcept {
		return stream_;
	}

	/// Closes the websocket connection.
	void close() {
		boost::beast::get_lowest_layer(stream()).close();
	}

	void start_running() {
		boost::asio::co_spawn(executor(), go(), boost::asio::detached);
	}

private:
	boost::asio::awaitable<void> go() {
		co_await connect();
		boost::asio::co_spawn(executor(), read_loop(), boost::asio::detached);
		boost::asio::co_spawn(executor(), write_loop(), boost::asio::detached);
	}

	/// Connects the client.
	boost::asio::awaitable<void> connect() {
		boost::asio::ip::tcp::resolver resolver{ executor() };

		const auto results = co_await resolver.async_resolve(TEST_CASE_HOSTNAME, TEST_CASE_SERVICE, boost::asio::use_awaitable);

#if !ASIO_STREAMS
		boost::beast::get_lowest_layer(stream()).expires_never();
#endif
#if !ASIO_STREAMS
		co_await boost::beast::get_lowest_layer(stream()).async_connect(results, boost::asio::use_awaitable);
#else
		co_await boost::asio::async_connect(boost::beast::get_lowest_layer(stream()) ,results, boost::asio::use_awaitable);
#endif

#if !WITHOUT_TLS
		co_await stream().next_layer().async_handshake(boost::asio::ssl::stream_base::client, boost::asio::use_awaitable);
#endif // !WITHOUT_TLS

		// Setup final timeout settings
		stream().set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));

		// Do the HTTP handshake
		co_await stream().async_handshake(TEST_CASE_HOSTNAME ":" TEST_CASE_SERVICE, "/", boost::asio::use_awaitable);
	}

	/// \brief Handles writing outgoing messages to the websocket as soon as
	/// they become available.
	boost::asio::awaitable<void> write_loop() {
		try {
			while ( true ) {
				const auto tx_message = co_await get_next_tx_message();

				co_await stream().async_write(boost::asio::buffer(tx_message), boost::asio::use_awaitable);
			} // while ( ... )
		} catch ( ... ) {
			close();
			throw;
		}
	}

	/// Handles reading incoming messages.
	boost::asio::awaitable<void> read_loop() {
		try {
			boost::beast::flat_buffer rx_buffer;

			while ( true ) {
				const auto bytes_transferred = co_await stream().async_read(rx_buffer, boost::asio::use_awaitable);
				const std::string_view data{ static_cast<const char *>(rx_buffer.data().data()), bytes_transferred };

				handle_incoming_message(data, stream().got_text());

				rx_buffer.consume(bytes_transferred);
				co_await wait_for_rx_ready();
			} // while ( ... )
		} catch ( ... ) {
			close();
			throw;
		}
	}

	/// \brief Returns the next message to be sent.
	/// For this test case the function has been reduced to simply provide a test
	/// string. In the real application this waits for new data to be queued up
	/// and return that instead.
	[[nodiscard]] boost::asio::awaitable<std::string> get_next_tx_message() {
		co_return "This is just a test";
	}

	/// \brief Waits for the client to receive further websocket messages.
	/// For this test case the function as been reduced to always return.
	/// Intended to be used for delaying new reads until data has been
	/// processed asynchronously if the worker pool is busy. The tcp buffer
	/// would fill up signaling the remote end to stop sending more data.
	[[nodiscard]] boost::asio::awaitable<void> wait_for_rx_ready() const {
		// Always ready.
		co_return;
	}

	/// \brief Handles incoming websocket messages.
	/// Does nothing for this test case.
	///
	/// \param[in] message The received message.
	/// \param[in] isText Whether the message is normal text (`true`) or binary
	///		(`false`).
	void handle_incoming_message([[maybe_unused]] std::string_view message, [[maybe_unused]] bool isText) const {
		// Nothing
	}

private:
	/// Executor used by asynchronous operations.
	executor_type executor_;
#if WITHOUT_TLS
	stream_type stream_{ executor() };
#else
	/// TLS context.
	boost::asio::ssl::context ctx_{ boost::asio::ssl::context::tlsv13_client };
	/// Underlying stream.
	stream_type stream_{ executor(), ctx_ };
#endif
};

/// \brief Websocket test server
/// Accepts only one client at a time and simply echos back messages sent by
/// the client.
class ws_test_server {
public:
	using executor_type = boost::asio::any_io_executor;
#if WITHOUT_TLS
	using stream_type = boost::beast::websocket::stream<tcp_layer>;
#else
	using stream_type = boost::beast::websocket::stream<ssl_layer>;
#endif

public:
	ws_test_server(executor_type executor)
		: executor_{ std::move(executor) }
	{
#if !WITHOUT_TLS
		ctx_.use_certificate(boost::asio::buffer(TEST_CERT), boost::asio::ssl::context_base::file_format::pem);
		ctx_.use_private_key(boost::asio::buffer(TEST_KEY), boost::asio::ssl::context_base::file_format::pem);
#endif // !WITHOUT_TLS
	}

	/// Returns the associated executor.
	[[nodiscard]] executor_type executor() const noexcept {
		return executor_;
	}

	void start_running() {
		boost::asio::co_spawn(executor(), open(), boost::asio::use_future).wait();
		boost::asio::co_spawn(executor(), accept_loop(), boost::asio::detached);
	}

private:
	boost::asio::awaitable<void> open() {
		boost::asio::ip::tcp::resolver resolver{ executor() };

		const auto resolve_results = co_await resolver.async_resolve(TEST_CASE_HOSTNAME, TEST_CASE_SERVICE, boost::asio::use_awaitable);
		if ( resolve_results.empty() ) {
			co_return;
		}

		const auto endpoint = resolve_results.begin()->endpoint();

		acceptor_.open(endpoint.protocol());

		// Allow address reuse on linux. On windows this allows for multiple programs
		// to listen on the same port at the same time and we don't want that with
		// tcp.
#if !defined(_WIN32)
		acceptor_.set_option(boost::asio::socket_base::reuse_address{ true });
#endif // !defined(_WIN32)

		// Bind to address.
		acceptor_.bind(endpoint);

		// Start listening.
		acceptor_.listen(acceptor_.max_listen_connections);
	}

	boost::asio::awaitable<void> accept_loop() {
		while ( true ) {
			boost::asio::ip::tcp::socket socket{ executor() };

			co_await acceptor_.async_accept(socket, boost::asio::use_awaitable);

			try {
				co_await session_loop(std::move(socket));
			} catch ( ... ) {
				// On error just wait for a new client.
			}
		} // while ( ... )
	}

	boost::asio::awaitable<void> session_loop(boost::asio::ip::tcp::socket socket) {
#if WITHOUT_TLS
		stream_type ws{ std::move(socket) };
#else
		stream_type ws{ std::move(socket), ctx_ };
#endif
		boost::beast::flat_buffer buffer;

#if !ASIO_STREAMS
		// Set timeout.
		boost::beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));
#endif

#if !WITHOUT_TLS
		// Perform TLS handshake.
		co_await ws.next_layer().async_handshake(boost::asio::ssl::stream_base::server, boost::asio::use_awaitable);
#endif // !WITHOUT_TLS

		// Turn off timeout on tcp_stream, because the websocket stream has its own
#if !ASIO_STREAMS
		// timeout system.
		boost::beast::get_lowest_layer(ws).expires_never();
#endif
		// Set suggested timeout settings for websocket.
		ws.set_option(
			boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server)
		);

		ws.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::response_type & res) {
			res.set(boost::beast::http::field::server, "ws coro assert test");
		}));

		co_await ws.async_accept(boost::asio::use_awaitable);

		// Just echo data back to the client
		while ( true ) {
			const auto bytes_transferred = co_await ws.async_read(buffer, boost::asio::use_awaitable);

			co_await ws.async_write(buffer.data(), boost::asio::use_awaitable);

			buffer.consume(bytes_transferred);
		} // while ( ... )
	}
private:
	/// Executor used by asynchronous operations.
	executor_type executor_;
#if !WITHOUT_TLS
	/// TLS context.
	boost::asio::ssl::context ctx_{ boost::asio::ssl::context::tlsv13_server };
#endif // WITHOUT_TLS
	boost::asio::ip::tcp::acceptor acceptor_{ executor() };
};

int main() {
	std::cout << "BOOST_VERSION = " << BOOST_VERSION << '\n'
		<< "BOOST_BEAST_VERSION = " << BOOST_BEAST_VERSION << '\n';

	constexpr unsigned int num_threads = 4;
	boost::asio::io_context ctx;

	try {
		for ( std::size_t i = 0; i < 10; ++i ) {
			std::cout << "Attempt #" << i << " ..." << '\n';
			boost::asio::thread_pool pool{ num_threads };
			auto work_guard = boost::asio::make_work_guard(ctx);

			ws_test_server server{ boost::asio::make_strand(ctx.get_executor()) };
			ws_test_client client{ boost::asio::make_strand(ctx.get_executor()) };
			// The issue gets triggered within the first few packages that were
			// exchanged. For simplicity we just cancel the current attempt
			// with a timer.
			boost::asio::steady_timer timer{ ctx };

			// Spawn worker threads
			for ( unsigned int j = 0; j < num_threads; ++j ) {
				boost::asio::post(pool, [&ctx]() {
					ctx.run();
				});
			} // for ( ... )

			// Start server
			server.start_running();
			// Start client
			client.start_running();

			// If the issue doesn't appear within 2 seconds stop and try again.
			timer.expires_from_now(std::chrono::seconds{ 2 });
			timer.wait();
			ctx.stop();
			pool.join();

			ctx.reset();
		}
	} catch ( const std::exception & e ) {
		std::cerr << "EXCEPT\n"
			<< "  what(): " << e.what() << '\n';
	}
}