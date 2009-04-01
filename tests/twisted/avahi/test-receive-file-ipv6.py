import avahi
import urllib
import BaseHTTPServer
import SocketServer
import socket

from saluttest import exec_test
from file_transfer_helper import ReceiveFileTest

from avahitest import AvahiAnnouncer, get_host_name, AvahiListener
from xmppstream import connect_to_stream6, setup_stream_listener6
from servicetest import TimeoutError

from twisted.words.xish import domish
import constants as cs

class TestReceiveFileIPv6(ReceiveFileTest):
    CONTACT_NAME = 'test-ft'

    def announce_contact(self, name=CONTACT_NAME):
        basic_txt = { "txtvers": "1", "status": "avail" }

        self.contact_name = '%s@%s' % (name, get_host_name())
        self.listener, port = setup_stream_listener6(self.q, self.contact_name)

        self.contact_service = AvahiAnnouncer(self.contact_name, "_presence._tcp", port,
                basic_txt, proto=avahi.PROTO_INET6)

    def wait_for_contact(self, name=CONTACT_NAME):
        publish_handle = self.conn.RequestHandles(cs.HT_CONTACT_LIST, ["publish"])[0]
        publish = self.conn.RequestChannel(
                "org.freedesktop.Telepathy.Channel.Type.ContactList",
                cs.HT_CONTACT_LIST, publish_handle, False)

        self.handle = 0
        # Wait until the record shows up in publish
        while self.handle == 0:
            try:
                e = self.q.expect('dbus-signal', signal='MembersChanged', path=publish)
            except TimeoutError:
                print "skip test as IPv6 doesn't seem to be enabled in Avahi"
                return True

            for h in e.args[1]:
                name = self.conn.InspectHandles(cs.HT_CONTACT, [h])[0]
                if name == self.contact_name:
                    self.handle = h

    def _resolve_salut_presence(self):
        AvahiListener(self.q).listen_for_service("_presence._tcp")
        e = self.q.expect('service-added', name = self.self_handle_name,
            protocol = avahi.PROTO_INET6)
        service = e.service
        service.resolve()

        e = self.q.expect('service-resolved', service = service)
        return str(e.pt), e.port

    def connect_to_salut(self):
        host, port = self._resolve_salut_presence()

        self.outbound = connect_to_stream6(self.q, self.contact_name,
            self.self_handle_name, host, port)

        e = self.q.expect('connection-result')
        assert e.succeeded, e.reason
        self.q.expect('stream-opened', connection = self.outbound)

    def send_ft_offer_iq(self):
        iq = domish.Element((None, 'iq'))
        iq['to'] = self.self_handle_name
        iq['from'] = self.contact_name
        iq['type'] = 'set'
        iq['id'] = 'gibber-file-transfer-0'
        query = iq.addElement(('jabber:iq:oob', 'query'))
        url = 'http://[::1]:%u/gibber-file-transfer-0/%s' % \
            (self.httpd.server_port, urllib.quote(self.file.name))
        url_node = query.addElement('url', content=url)
        url_node['type'] = 'file'
        url_node['size'] = str(self.file.size)
        url_node['mimeType'] = self.file.content_type
        query.addElement('desc', content=self.file.description)
        self.outbound.send(iq)

    def _get_http_server_class(self):
        class HTTPServer6(SocketServer.ThreadingMixIn, BaseHTTPServer.HTTPServer):
            address_family = getattr(socket, 'AF_INET6', None)

        return HTTPServer6

if __name__ == '__main__':
    test = TestReceiveFileIPv6()
    exec_test(test.test)
