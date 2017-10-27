/**
 * main.c
 *
 * Copyright (C) 2017 Nickolas Burr <nickolasburr@gmail.com>
 */

#include "main.h"

int main (int argc, char **argv) {
	size_t crt_index;
	int opt_index, arg_index, last_index;
	int chain, cypher, serial, server, raw;
	char hostname[MAX_HOSTNAME_LENGTH],
	     port[MAX_PORT_LENGTH],
	     scheme[MAX_SCHEME_LENGTH],
	     url[MAX_URL_LENGTH],
	     methodname[MAX_METHOD_LENGTH],
	     ciphername[MAX_CIPHER_LENGTH];
	const SSL_CIPHER *cipher;
	const SSL_METHOD *method;
	ASN1_INTEGER *asn1_serial;
	STACK_OF(X509) *fullchain;
	BIO *bp;
	X509 *crt, *tcrt;
	X509_NAME *crtname, *tcrtname;
	SSL_CTX *ctx;
	SSL *ssl;

	last_index = (argc - 1);
	server = 0;

	/**
	 * Hide full certificate chain,
	 * suppress raw certificate output,
	 * suppress serial number, by default.
	 */
	cypher = 0;
	chain = 0;
	raw = 0;
	serial = 0;

	/**
	 * If --chain option was given, output
	 * the entire certificate chain.
	 */
	if (in_array("--chain", argv, argc) ||
	    in_array("-c", argv, argc)) {

		chain = 1;
	}

	/**
	 * If --raw option was given, output
	 * raw certificate contents to stdout.
	 */
	if (in_array("--raw", argv, argc) ||
	    in_array("-r", argv, argc)) {

		raw = 1;
	}

	/**
	 * If --serial option was given, output
	 * serial number for the certificate(s).
	 */
	if (in_array("--serial", argv, argc) ||
	    in_array("-s", argv, argc)) {

		serial = 1;
	}

	/**
	 * Check if --hostname option was given.
	 * If not, throw an exception and exit.
	 */
	if (in_array("--hostname", argv, argc) ||
	    in_array("-H", argv, argc)) {

		opt_index = (index_of("--hostname", argv, argc) != NOT_FOUND)
		          ? index_of("--hostname", argv, argc)
		          : index_of("-H", argv, argc);

		if ((arg_index = (opt_index + 1)) > last_index) {
			fprintf(stderr, "--hostname: Missing argument\n");

			exit(EXIT_FAILURE);
		}

		copy(hostname, argv[arg_index]);
	} else {
		fprintf(stderr, "Error: Missing --hostname\n");

		exit(EXIT_FAILURE);
	}

	/**
	 * Check if --scheme option was given.
	 * If not, set default scheme to https.
	 *
	 * @todo: Whitelist schemes and throw an
	 *        exception on unsupported schemes.
	 */
	if (in_array("--scheme", argv, argc) ||
	    in_array("-S", argv, argc)) {

		opt_index = (index_of("--scheme", argv, argc) != NOT_FOUND)
		          ? index_of("--scheme", argv, argc)
		          : index_of("-S", argv, argc);

		if ((arg_index = (opt_index + 1)) > last_index) {
			fprintf(stderr, "--scheme: Missing argument\n");

			exit(EXIT_FAILURE);
		}

		copy(scheme, argv[arg_index]);
	} else {
		copy(scheme, "https");
	}

	/**
	 * Assemble URL for request.
	 */
	copy(url, scheme);
	concat(url, "://");
	concat(url, hostname);

	/**
	 * Run OpenSSL initialization tasks.
	 */
	OpenSSL_add_all_algorithms();
	ERR_load_BIO_strings();
	ERR_load_crypto_strings();
	SSL_load_error_strings();

	/**
	 * Initialize OpenSSL library.
	 */
	SSL_library_init();

	/**
	 * Check if --method option was given.
	 * If not, method will be negotiated.
	 */
	if (in_array("--method", argv, argc) ||
	    in_array("-M", argv, argc)) {

		opt_index = (index_of("--method", argv, argc) != NOT_FOUND)
		          ? index_of("--method", argv, argc)
		          : index_of("-M", argv, argc);

		if ((arg_index = (opt_index + 1)) > last_index) {
			fprintf(stderr, "--method: Missing argument\n");

			exit(EXIT_FAILURE);
		}

		copy(methodname, argv[arg_index]);

		/**
		 * Force specific method for handshake.
		 */
		switch (get_bitmask_from_key(methodname)) {
			case OPT_SSLV2:
				method = SSLv2_client_method();

				/**
				 * Manually clear SSL_OP_NO_SSLv2 so we
				 * can downgrade the handshake properly.
				 */
				SSL_CTX_clear_options(ctx, SSL_OP_NO_SSLv2);
				SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);

				break;
			case OPT_SSLV3:
				method = SSLv3_client_method();
				SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);

				break;
			case OPT_TLSV1:
				method = TLSv1_client_method();
				SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);

				break;
			case OPT_TLSV1_1:
				method = TLSv1_1_client_method();
				SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_2);

				break;
			case OPT_TLSV1_2:
				method = TLSv1_2_client_method();
				SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

				break;
			default:
				fprintf(stderr, "%s is not a valid method type\n", methodname);

				exit(EXIT_FAILURE);
		}
	} else {
		method = SSLv23_client_method();
	}

	/**
	 * Initialize BIO stream pointer.
	 */
	bp = BIO_new_fp(stdout, BIO_NOCLOSE);

	/**
	 * Establish new SSL context.
	 */
	if (is_null(ctx = SSL_CTX_new(method))) {
		BIO_printf(bp, "Unable to establish new SSL context structure.\n");

		goto on_error;
	}

	SSL_CTX_set_ssl_version(ctx, method);

	/**
	 * Make TCP socket connection.
	 *
	 * @todo: Add check, throw exception on failure.
	 */
	server = mksock(url, bp);

	/**
	 * Establish connection, get cipher.
	 */
	ssl = SSL_new(ctx);

	/**
	 * Manually set state in client mode.
	 */
	SSL_set_connect_state(ssl);

	/**
	 * Check if --cipher option was given.
	 * If not, negotiate during handshake.
	 *
	 * @todo: This currently allows any string
	 *        to be set as the cipher name. Instead,
	 *        get a list of ciphers currently available
	 *        on this system and throw an exception if
	 *        an invalid cipher name was given.
	 */
	if (in_array("--cipher", argv, argc) ||
	    in_array("-C", argv, argc)) {

		opt_index = (index_of("--cipher", argv, argc) != NOT_FOUND)
		          ? index_of("--cipher", argv, argc)
		          : index_of("-C", argv, argc);

		if ((arg_index = (opt_index + 1)) > last_index) {
			fprintf(stderr, "--cipher: Missing argument\n");

			exit(EXIT_FAILURE);
		}

		cypher = 1;

		/**
		 * @todo: See above to fix this. It's very dangerous.
		 */
		SSL_set_cipher_list(ssl, argv[arg_index]);
		cipher = SSL_get_current_cipher(ssl);

		copy(ciphername, SSL_CIPHER_get_name(cipher));
	} else {
		cipher = SSL_get_current_cipher(ssl);

		copy(ciphername, SSL_CIPHER_get_name(cipher));
	}

	/**
	 * Attach the SSL session to the socket.
	 */
	if (is_error(SSL_set_fd(ssl, server))) {
		BIO_printf(bp, "Error: Unable to attach SSL session to socket.\n");
	}

	/**
	 * Bridge the connection.
	 */
	if (SSL_connect(ssl) != 1) {
		BIO_printf(bp, "Error: Could not build an SSL session to %s.\n", url);

		goto on_error;
	}

	if (cypher) {
		/**
		 * Get current cipher.
		 */
		BIO_printf(bp, "--- Cipher: %s\n", ciphername);
	}

	if (chain) {
		/**
		 * Get certificate chain.
		 */
		if (is_null(fullchain = SSL_get_peer_cert_chain(ssl))) {
			BIO_printf(bp, "Error: Could not get certificate chain from %s.\n", url);

			goto on_error;
		}

		BIO_printf(bp, "--- Certificate Chain:\n");

		for (crt_index = 0; crt_index < sk_X509_num(fullchain); crt_index += 1) {
			tcrt = sk_X509_value(fullchain, crt_index);
			tcrtname = X509_get_subject_name(tcrt);

			/**
			 * Output certificate chain.
			 */
			BIO_printf(bp, "%5d:", (int) crt_index);
			X509_NAME_print_ex(bp, tcrtname, 1, XN_FLAG_SEP_CPLUS_SPC);

			/**
			 * If --serial option was given, output ASN1 serial.
			 */
			if (serial) {
				BIO_printf(bp, ", Serial=");
				i2a_ASN1_INTEGER(bp, X509_get_serialNumber(tcrt));
			}

			BIO_printf(bp, "\n");
		}

		/**
		 * Output raw certificate contents if --raw option was specified.
		 */
		if (raw) {
			BIO_printf(bp, "\n");

			for (crt_index = 0; crt_index < sk_X509_num(fullchain); crt_index += 1) {
				PEM_write_bio_X509(bp, sk_X509_value(fullchain, crt_index));
				BIO_printf(bp, "\n");
			}
		}
	} else {
		/**
		 * Load remote certificate into X509 struct.
		 */
		if (is_null(crt = SSL_get_peer_certificate(ssl))) {
			BIO_printf(bp, "Error: Could not get certificate from %s.\n", url);

			goto on_error;
		}

		/**
		 * Get certificate subject, serial number.
		 */
		crtname = X509_get_subject_name(crt);

		/**
		 * Output certificate subject.
		 */
		BIO_printf(bp, "--- Subject: ");
		X509_NAME_print_ex(bp, crtname, 0, XN_FLAG_SEP_COMMA_PLUS);
		BIO_printf(bp, "\n");

		/**
		 * If --serial option was given, output ASN1 serial.
		 */
		if (serial) {
			BIO_printf(bp, "--- Serial: ");
			i2a_ASN1_INTEGER(bp, X509_get_serialNumber(crt));
			BIO_printf(bp, "\n");
		}

		/**
		 * @todo: Output raw certficate contents if --raw was given.
		 */
	}

	/**
	 * Run cleanup tasks.
	 */
	BIO_free(bp);
	SSL_free(ssl);
	close(server);
	X509_free(crt);
	SSL_CTX_free(ctx);
	ERR_free_strings();

	return EXIT_SUCCESS;

on_error:
	BIO_printf(bp, "\n");
	ERR_print_errors(bp);
	BIO_free(bp);
	SSL_free(ssl);
	close(server);
	X509_free(crt);
	SSL_CTX_free(ctx);
	ERR_free_strings();

	return EXIT_FAILURE;
}
