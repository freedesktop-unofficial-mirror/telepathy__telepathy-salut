
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities.DRAFT

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, to test whether the caps cache
  works with tubes

2. Test SetSelfCapabilities and test that the avahi txt record is updated test
that the D-Bus signal ContactCapabilitiesChanged is fired for the self handle,
ask Salut for its caps with an iq request, check the reply is correct, and ask
Salut for its caps using D-Bus method GetContactCapabilities. Also check that
GetContactAttributes gives the same results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, just for the fun

"""

import dbus
import sys

from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
from avahitest import txt_get_key
import avahi

from twisted.words.xish import domish, xpath

from servicetest import EventPattern
from saluttest import exec_test, make_result_iq, sync_stream
from xmppstream import setup_stream_listener, connect_to_stream

HT_CONTACT = 1
HT_CONTACT_LIST = 3

text_iface = 'org.freedesktop.Telepathy.Channel.Type.Text'
caps_iface = 'org.freedesktop.Telepathy.' + \
             'Connection.Interface.ContactCapabilities.DRAFT'
contacts_iface = 'org.freedesktop.Telepathy.Connection.Interface.Contacts'

ns_tubes = 'http://telepathy.freedesktop.org/xmpp/tubes'

text_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.Text'
    })
text_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

daap_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
        'daap'
    })
daap_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

http_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
        'http'
    })
http_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

xiangqi_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
        'com.example.Xiangqi'
    })
xiangqi_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

go_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
        'com.example.Go'
    })
go_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

def check_caps(txt, ver=None):
    for (key, val) in { "1st": "test",
                        "last": "suite",
                        "status": "avail",
                        "txtvers": "1" }.iteritems():
        v =  txt_get_key(txt, key)
        assert v == val, (key, val, v)

    if ver is None:
        assert txt_get_key(txt, "hash") is None
        assert txt_get_key(txt, "node") is None
        assert txt_get_key(txt, "ver") is None
    else:
        assert txt_get_key(txt, "hash") == "sha-1"
        assert txt_get_key(txt, "node") == NS_TELEPATHY_CAPS

        v = txt_get_key(txt, "ver")
        assert v == ver, (v, ver)


def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def presence_add_caps(presence, ver, client, hash=None):
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = ver
    if hash is not None:
        c['hash'] = hash
    return presence

# last value of the "ver" key we resolved. We use it to be sure that the
# modified caps has already be announced.
old_ver = ''

def receive_presence_and_ask_caps(q, stream, service):
    global old_ver

    event_avahi, event_dbus = q.expect_many(
            EventPattern('service-resolved', service=service),
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )
    assert event_dbus.args[0] == 1
    signaled_caps = event_dbus.args[1]

    ver = txt_get_key(event_avahi.txt, "ver")
    while ver == old_ver:
        # be sure that the announced caps actually changes
        event_avahi = q.expect('service-resolved', service=service)
        ver = txt_get_key(event_avahi.txt, "ver")
    old_ver = ver

    hash = txt_get_key(event_avahi.txt, "hash")
    node = txt_get_key(event_avahi.txt, "node")
    assert hash == 'sha-1'

    # ask caps
    request = """
<iq from='fake_contact@jabber.org/resource' 
    id='disco1'
    to='salut@jabber.org/resource' 
    type='get'>
  <query xmlns='http://jabber.org/protocol/disco#info'
         node='""" + node + '#' + ver + """'/>
</iq>
"""
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq',
        query_ns='http://jabber.org/protocol/disco#info')
    caps_str = str(xpath.queryForNodes('/iq/query/feature', event.stanza))

    return (event, caps_str, signaled_caps)

def caps_contain(event, cap):
    node = xpath.queryForNodes('/iq/query/feature[@var="%s"]'
            % cap,
            event.stanza)
    if node is None:
        return False
    if len(node) != 1:
        return False
    var = node[0].attributes['var']
    if var is None:
        return False
    return var == cap

def test_tube_caps_from_contact(q, bus, conn, service,
        client):

    conn_caps_iface = dbus.Interface(conn, caps_iface)
    conn_contacts_iface = dbus.Interface(conn, contacts_iface)

    # send presence with no tube cap
    ver = 'JpaYgiKL0y4fUOCTwN3WLGpaftM='
    txt_record = { "txtvers": "1", "status": "avail",
        "node": client, "ver": ver, "hash": "sha-1"}
    contact_name = "test-caps-tube@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # this is the first presence, Salut connects to the contact
    e = q.expect('incoming-connection', listener = listener)
    incoming = e.connection

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + ver, (query_node.attributes['node'], client, ver)

    contact_handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + ver

    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    incoming.send(result)

    # no change in ContactCapabilities, so no signal ContactCapabilitiesChanged
    sync_stream(q, incoming)

    # no special capabilities
    basic_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
            caps_via_contacts_iface

    # send presence with 1 stream tube cap
    txt_record['ver'] = 'f5oUAlH0fcR8btEo5K0P135QReo='
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#daap'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert event.args[0] == contact_handle
    signaled_caps = event.args[1]
    assert len(signaled_caps) == 2, signaled_caps # basic caps + daap
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'

    # daap capabilities
    daap_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with 1 D-Bus tube cap
    txt_record['ver'] = '4Ps2iaOc+lsFwfbasCdsBjLOQ5s='
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert event.args[0] == contact_handle
    signaled_caps = event.args[1]
    assert len(signaled_caps) == 2, signaled_caps # basic caps + Xiangqi
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # xiangqi capabilities
    xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    txt_record['ver'] = 'ALCBfacl4M/FKWckV1OCHfj+lt0='
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#daap'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert event.args[0] == contact_handle
    signaled_caps = event.args[1]
    assert len(signaled_caps) == 3, signaled_caps # basic caps + daap+xiangqi
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with 4 tube caps
    txt_record['ver'] = 'ObSHJf9W0fUDuSjmB6gmthptw+s='
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus#com.example.Go'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#daap'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#http'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert event.args[0] == contact_handle
    signaled_caps = event.args[1]
    assert len(signaled_caps) == 5, signaled_caps # basic caps + 4 tubes
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'http'
    assert signaled_caps[3][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'
    assert signaled_caps[4][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Go'

    # http + daap + xiangqi + go capabilities
    all_tubes_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties,
                xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    txt_record['ver'] = 'ALCBfacl4M/FKWckV1OCHfj+lt0='
    announcer.update(txt_record)

    # Salut does not look up our capabilities because of the cache

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert event.args[0] == contact_handle
    signaled_caps = event.args[1]
    assert len(signaled_caps) == 3, signaled_caps # basic caps + daap+xiangqi
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [caps_iface], False) \
            [contact_handle][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

def test_tube_caps_to_contact(q, bus, conn, service):
    basic_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties)]})
    daap_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties)]})
    xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    daap_xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    all_tubes_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})

    # send presence with no cap info
    txt_record = { "txtvers": "1", "status": "avail"}
    contact_name = "test-caps-tube2@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # Before opening a connection to Salut, wait Salut receives our presence
    # via Avahi. Otherwise, Salut will not allow our connection. We may
    # consider it is a bug in Salut, and we may want Salut to wait a few
    # seconds in case Avahi was slow.
    # See incoming_pending_connection_got_from(): if the SalutContact is not
    # found in the table, we close the connection.
    q.expect('dbus-signal', signal='PresencesChanged')

    # initialise a connection (Salut does not do it because there is no caps
    # here)
    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]
    service.resolve()
    e = q.expect('service-resolved', service = service)
    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)
    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    conn_caps_iface = dbus.Interface(conn, caps_iface)
    conn_contacts_iface = dbus.Interface(conn, contacts_iface)

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise nothing
    conn_caps_iface.SetSelfCapabilities([])

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    sync_stream(q, outbound)

    # Advertise daap
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == False, caps_str
    assert len(signaled_caps) == 2, signaled_caps # basic caps + daap
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [xiangqi_fixed_properties])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == False, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 2, signaled_caps # basic caps + daap
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 3, signaled_caps # basic caps + daap+xiangqi
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise 4 tubes
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, http_fixed_properties,
         go_fixed_properties, xiangqi_fixed_properties])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == True, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == True, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 5, signaled_caps # basic caps + 4 tubes
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'http'
    assert signaled_caps[3][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'
    assert signaled_caps[4][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Go'

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
service)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 3, signaled_caps # basic caps + daap+xiangqi
    assert signaled_caps[1][0] \
        ['org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service'] \
        == 'daap'
    assert signaled_caps[2][0] \
        ['org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName'] \
        == 'com.example.Xiangqi'

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [caps_iface], False) \
            [1][caps_iface + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface


def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 0])

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(1, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)
    check_caps(e.txt)

    client = 'http://telepathy.freedesktop.org/fake-client'

    test_tube_caps_from_contact(q, bus, conn, service,
            client)

    test_tube_caps_to_contact(q, bus, conn, service)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

