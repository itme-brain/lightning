#include "config.h"
#include "../../wire/fromwire.c"
#include "../../wire/tlvstream.c"
#include "../../wire/towire.c"
#include "../amount.c"
#include "../bigsize.c"
#include "../blindedpath.c"
#include "../blindedpay.c"
#include "../blinding.c"
#include "../features.c"
#include "../hmac.c"
#include "../json_parse.c"
#include "../json_parse_simple.c"
#include "../onion_encode.c"
#include "../sphinx.c"
#include "../../wire/onion_wiregen.c"
#include <ccan/tal/grab_file/grab_file.h>
#include <ccan/tal/path/path.h>
#include <common/bolt12.h>
#include <common/channel_id.h>
#include <common/json_parse.h>
#include <common/json_stream.h>
#include <common/setup.h>
#include <common/wireaddr.h>
#include <stdio.h>

/* AUTOGENERATED MOCKS START */
/* Generated stub for fromwire_sciddir_or_pubkey */
void fromwire_sciddir_or_pubkey(const u8 **cursor UNNEEDED, size_t *max UNNEEDED,
				struct sciddir_or_pubkey *sciddpk UNNEEDED)
{ fprintf(stderr, "fromwire_sciddir_or_pubkey called!\n"); abort(); }
/* Generated stub for mvt_tag_str */
const char *mvt_tag_str(enum mvt_tag tag UNNEEDED)
{ fprintf(stderr, "mvt_tag_str called!\n"); abort(); }
/* Generated stub for new_onionreply */
struct onionreply *new_onionreply(const tal_t *ctx UNNEEDED, const u8 *contents TAKES UNNEEDED)
{ fprintf(stderr, "new_onionreply called!\n"); abort(); }
/* Generated stub for node_id_from_hexstr */
bool node_id_from_hexstr(const char *str UNNEEDED, size_t slen UNNEEDED, struct node_id *id UNNEEDED)
{ fprintf(stderr, "node_id_from_hexstr called!\n"); abort(); }
/* Generated stub for pubkey_from_node_id */
bool pubkey_from_node_id(struct pubkey *key UNNEEDED, const struct node_id *id UNNEEDED)
{ fprintf(stderr, "pubkey_from_node_id called!\n"); abort(); }
/* Generated stub for towire_sciddir_or_pubkey */
void towire_sciddir_or_pubkey(u8 **pptr UNNEEDED,
			      const struct sciddir_or_pubkey *sciddpk UNNEEDED)
{ fprintf(stderr, "towire_sciddir_or_pubkey called!\n"); abort(); }
/* AUTOGENERATED MOCKS END */

#if 0
/* Updated each time, as we pretend to be Alice, Bob, Carol */
static struct secret mykey;

static void test_ecdh(const struct pubkey *point, struct secret *ss)
{
	if (secp256k1_ecdh(secp256k1_ctx, ss->data, &point->pubkey,
			   mykey.data, NULL, NULL) != 1)
		abort();
}
#endif

static bool json_to_tok(const char *buffer, const jsmntok_t *tok,
			const jsmntok_t **tokp)
{
	*tokp = tok;
	return true;
}

int main(int argc, char *argv[])
{
	char *json;
	size_t i;
	jsmn_parser parser;
	jsmntok_t toks[5000];
	const jsmntok_t *t, *hops_tok;
	struct blinded_path *bpath;
	struct pubkey *ids;
	u8 **onionhops, *associated_data, *onion, *expected_onion;
	struct short_channel_id initscid;
	struct sphinx_path *sp;
	struct secret session_key, *path_secrets;
	u32 final_cltv;
	struct amount_msat initial_amount, final_amount;
	u32 path_fee_base_msat, path_fee_proportional_millionths, path_cltv_delta;

	common_setup(argv[0]);

	if (argv[1])
		json = grab_file(tmpctx, argv[1]);
	else {
		char *dir = getenv("BOLTDIR");
		json = grab_file(tmpctx,
				 path_join(tmpctx,
					   dir ? dir : ".tmp.lightningrfc",
					   "bolt04/blinded-payment-onion-test.json"));
		if (!json) {
			printf("test file not found, skipping\n");
			goto out;
		}
	}

	jsmn_init(&parser);
	if (jsmn_parse(&parser, json, strlen(json), toks, ARRAY_SIZE(toks)) < 0)
		abort();

	bpath = tal(tmpctx, struct blinded_path);

	assert(json_scan(tmpctx, json, toks,
			 "{generate:{session_key:%,"
			 "associated_data:%,"
			 "final_amount_msat:%,"
			 "final_cltv:%,"
			 "blinded_payinfo:{fee_base_msat:%,fee_proportional_millionths:%,cltv_expiry_delta:%},"
			 "blinded_route:{first_node_id:%,first_path_key:%,hops:%}}}",
			 JSON_SCAN(json_to_secret, &session_key),
			 JSON_SCAN_TAL(tmpctx, json_tok_bin_from_hex, &associated_data),
			 JSON_SCAN(json_to_msat, &final_amount),
			 JSON_SCAN(json_to_u32, &final_cltv),
			 JSON_SCAN(json_to_u32, &path_fee_base_msat),
			 JSON_SCAN(json_to_u32, &path_fee_proportional_millionths),
			 JSON_SCAN(json_to_u32, &path_cltv_delta),
			 JSON_SCAN(json_to_pubkey, &bpath->first_node_id.pubkey),
			 JSON_SCAN(json_to_pubkey, &bpath->first_path_key),
			 JSON_SCAN(json_to_tok, &hops_tok)) == NULL);

	/* FIXME: Test scid as well! */
	bpath->first_node_id.is_pubkey = true;
	bpath->path = tal_arr(bpath, struct blinded_path_hop *, hops_tok->size);
	json_for_each_arr(i, t, hops_tok) {
		bpath->path[i] = tal(bpath->path, struct blinded_path_hop);
		assert(json_scan(tmpctx, json, t, "{blinded_node_id:%,encrypted_data:%}",
				 JSON_SCAN(json_to_pubkey,
					   &bpath->path[i]->blinded_node_id),
				 JSON_SCAN_TAL(bpath->path[i],
					       json_tok_bin_from_hex,
					       &bpath->path[i]->encrypted_recipient_data)) == NULL);
	}

	assert(json_scan(tmpctx, json, toks, "{generate:{full_route:{hops:%}}}",
			 JSON_SCAN(json_to_tok, &hops_tok)) == NULL);

	/* We have to read scid from first hop contents, since it's made up */
	assert(json_scan(tmpctx, json, hops_tok + 1, "{tlvs:{outgoing_channel_id:%}}",
			 JSON_SCAN(json_to_short_channel_id, &initscid)) == NULL);

	initial_amount = final_amount;
	assert(amount_msat_add_fee(&initial_amount,
				   path_fee_base_msat, path_fee_proportional_millionths));

	/* FIXME: Test vector actually claims total_amount_msat is 150000msat! */
	struct amount_msat total_amount;
	assert(amount_msat_add(&total_amount, final_amount, AMOUNT_MSAT(50000)));
	onionhops = blinded_onion_hops(tmpctx, final_amount, final_cltv, total_amount, bpath);

	/* Prepend Alice: poor thing doesn't speak blinding!  (But doesn't charge fees!) */
	tal_resize(&onionhops, tal_count(onionhops) + 1);
	memmove(onionhops + 1, onionhops,
		(tal_count(onionhops) - 1) * sizeof(*onionhops));
	onionhops[0] = onion_nonfinal_hop(onionhops, &initscid,
					  initial_amount, final_cltv + path_cltv_delta);

	ids = tal_arr(tmpctx, struct pubkey, hops_tok->size);
	json_for_each_arr(i, t, hops_tok) {
		u8 *payload;
		assert(json_scan(tmpctx, json, t, "{payload:%,pubkey:%}",
				 JSON_SCAN_TAL(tmpctx,
					       json_tok_bin_from_hex,
					       &payload),
				 JSON_SCAN(json_to_pubkey, &ids[i])) == NULL);
		assert(tal_arr_eq(payload, onionhops[i]));
	}

	/* Now, create onion! */
	sp = sphinx_path_new_with_key(tmpctx, associated_data, &session_key);
	for (i = 0; i < tal_count(ids); i++)
		sphinx_add_hop_has_length(sp, &ids[i], onionhops[i]);

	onion = serialize_onionpacket(tmpctx,
				      create_onionpacket(tmpctx, sp, ROUTING_INFO_SIZE,
							 &path_secrets));
	assert(json_scan(tmpctx, json, toks, "{generate:{onion:%}}",
			 JSON_SCAN_TAL(tmpctx,
				       json_tok_bin_from_hex,
				       &expected_onion)) == NULL);
	assert(tal_arr_eq(expected_onion, onion));

	/* FIXME: unwrap and test! */
#if 0
	struct onionpacket *op;
	struct pubkey *blinding;

	assert(json_scan(tmpctx, json, toks, "{decrypt:{hops:%}}",
			 JSON_SCAN(json_to_tok, &hops_tok)) == NULL);
	op = parse_onionpacket(tmpctx, expected_onion, tal_bytelen(expected_onion), NULL);
	blinding = NULL;
	json_for_each_arr(i, t, hops_tok) {
		struct route_step *rs;
		struct secret ss;
		const u8 *serialized;

		assert(json_scan(tmpctx, json, t, "{node_privkey:%,onion:%}",
				 JSON_SCAN(json_to_secret, &mykey),
				 JSON_SCAN_TAL(tmpctx, json_tok_bin_from_hex, &expected_onion))
		       == NULL);
		serialized = serialize_onionpacket(tmpctx, op);
		assert(tal_arr_eq(expected_onion, serialized));

		if (blinding) {
			assert(unblind_onion(blinding, test_ecdh,
					     &op->ephemeralkey, &ss));
		} else {
			test_ecdh(&op->ephemeralkey, &ss);
		}
		rs = process_onionpacket(tmpctx, op, &ss, associated_data,
					 tal_bytelen(associated_data), true);
		assert(tal_arr_eq(rs->raw_payload, onionhops[i]));
		if (rs->nextcase == ONION_FORWARD)
			op = rs->next;
		else
			op = NULL;
		blinding = tal(tmpctx, struct pubkey);
		/* Alice doesn't have a blinding! */
		if (json_scan(tmpctx, json, t, "{next_blinding:%}",
			      JSON_SCAN(json_to_pubkey, blinding)) != NULL)
			blinding = NULL;
	}
	assert(!op);
#endif

out:
	common_shutdown();
}
