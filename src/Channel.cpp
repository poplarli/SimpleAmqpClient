
/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * ``The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is SimpleAmqpClient for RabbitMQ.
 *
 * The Initial Developer of the Original Code is Alan Antonuk.
 * Original code is Copyright (C) Alan Antonuk.
 *
 * All Rights Reserved.
 *
 * Contributor(s): ______________________________________.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 2 or later (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of those
 * above. If you wish to allow use of your version of this file only
 * under the terms of the GPL, and not to allow others to use your
 * version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the
 * notice and other provisions required by the GPL. If you do not
 * delete the provisions above, a recipient may use your version of
 * this file under the terms of any one of the MPL or the GPL.
 *
 * ***** END LICENSE BLOCK *****
 */

#include "SimpleAmqpClient/Channel.h"

#include "SimpleAmqpClient/AmqpResponseLibraryException.h"
#include "SimpleAmqpClient/AmqpResponseServerException.h"
#include "SimpleAmqpClient/ConsumerTagNotFoundException.h"
#include "SimpleAmqpClient/MessageReturnedException.h"
#include "SimpleAmqpClient/Util.h"
#include "SimpleAmqpClient/ChannelImpl.h"
#include "config.h"

#include <amqp.h>
#include <amqp_framing.h>

#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/chrono.hpp>
#include <boost/cstdint.hpp>
#include <boost/limits.hpp>

namespace AmqpClient {

const std::string Channel::EXCHANGE_TYPE_DIRECT("amq.direct");
const std::string Channel::EXCHANGE_TYPE_FANOUT("fanout");
const std::string Channel::EXCHANGE_TYPE_TOPIC("topic");

Channel::Channel(const std::string& host,
                 int port,
                 const std::string& username,
                 const std::string& password,
                 const std::string& vhost,
                 int frame_max) :
m_impl(new Detail::ChannelImpl)
{
    m_impl->m_connection = amqp_new_connection();

    int sock = amqp_open_socket(host.c_str(), port);
    m_impl->CheckForError(sock, "Channel::Channel amqp_open_socket");

    amqp_set_sockfd(m_impl->m_connection, sock);

    m_impl->CheckRpcReply(0, amqp_login(m_impl->m_connection, vhost.c_str(), 0,
                                   frame_max, BROKER_HEARTBEAT, AMQP_SASL_METHOD_PLAIN,
                                   username.c_str(), password.c_str()), "Channel::Channel amqp_login");
}

Channel::~Channel()
{
    amqp_connection_close(m_impl->m_connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(m_impl->m_connection);
}

void Channel::DeclareExchange(const std::string& exchange_name,
                              const std::string& exchange_type,
                              bool passive,
                              bool durable,
                              bool auto_delete)
{
  static const boost::array<uint32_t, 1> DECLARE_OK = { { AMQP_EXCHANGE_DECLARE_OK_METHOD } };

  amqp_exchange_declare_t declare;
  declare.exchange = amqp_cstring_bytes(exchange_name.c_str());
  declare.type = amqp_cstring_bytes(exchange_type.c_str());
  declare.passive = passive;
  declare.durable = durable;
  declare.auto_delete = auto_delete;
  declare.internal = false;
  declare.nowait = false;
  declare.arguments = AMQP_EMPTY_TABLE;
  
  m_impl->DoRpc(AMQP_EXCHANGE_DECLARE_METHOD, &declare, DECLARE_OK);
}

void Channel::DeleteExchange(const std::string& exchange_name,
                             bool if_unused)
{
  static const boost::array<uint32_t, 1> DELETE_OK = { { AMQP_EXCHANGE_DELETE_OK_METHOD } };

  amqp_exchange_delete_t del;
  del.exchange = amqp_cstring_bytes(exchange_name.c_str());
  del.if_unused = if_unused;
  del.nowait = false;

  m_impl->DoRpc(AMQP_EXCHANGE_DELETE_METHOD, &del, DELETE_OK);
}

void Channel::BindExchange(const std::string& destination,
                           const std::string& source,
                           const std::string& routing_key)
{
  static const boost::array<uint32_t, 1> BIND_OK = { { AMQP_EXCHANGE_BIND_OK_METHOD } };

  amqp_exchange_bind_t bind;
  bind.destination = amqp_cstring_bytes(destination.c_str());
  bind.source = amqp_cstring_bytes(source.c_str());
  bind.routing_key = amqp_cstring_bytes(routing_key.c_str());
  bind.nowait = false;
  bind.arguments = AMQP_EMPTY_TABLE;

  m_impl->DoRpc(AMQP_EXCHANGE_BIND_METHOD, &bind, BIND_OK);
}

void Channel::UnbindExchange(const std::string& destination,
                             const std::string& source,
                             const std::string& routing_key)
{
  static const boost::array<uint32_t, 1> UNBIND_OK = { { AMQP_EXCHANGE_UNBIND_OK_METHOD } };

  amqp_exchange_unbind_t unbind;
  unbind.destination = amqp_cstring_bytes(destination.c_str());
  unbind.source = amqp_cstring_bytes(source.c_str());
  unbind.routing_key = amqp_cstring_bytes(routing_key.c_str());
  unbind.nowait = false;
  unbind.arguments = AMQP_EMPTY_TABLE;

  m_impl->DoRpc(AMQP_EXCHANGE_UNBIND_METHOD, &unbind, UNBIND_OK);
}

std::string Channel::DeclareQueue(const std::string& queue_name,
                                  bool passive,
                                  bool durable,
                                  bool exclusive,
                                  bool auto_delete)
{
  static const boost::array<uint32_t, 1> DECLARE_OK = { { AMQP_QUEUE_DECLARE_OK_METHOD } };

  amqp_queue_declare_t declare;
  declare.queue = amqp_cstring_bytes(queue_name.c_str());
  declare.passive = passive;
  declare.durable = durable;
  declare.exclusive = exclusive;
  declare.auto_delete = auto_delete;
  declare.nowait = false;
  declare.arguments = AMQP_EMPTY_TABLE;

  amqp_frame_t response = m_impl->DoRpc(AMQP_QUEUE_DECLARE_METHOD, &declare, DECLARE_OK);

  amqp_queue_declare_ok_t* declare_ok = (amqp_queue_declare_ok_t*)response.payload.method.decoded;

  return std::string((char*)declare_ok->queue.bytes, declare_ok->queue.len);
}

void Channel::DeleteQueue(const std::string& queue_name,
                          bool if_unused,
                          bool if_empty)
{
  static const boost::array<uint32_t, 1> DELETE_OK = { { AMQP_QUEUE_DELETE_OK_METHOD } };

  amqp_queue_delete_t del;
  del.queue = amqp_cstring_bytes(queue_name.c_str());
  del.if_unused = if_unused;
  del.if_empty = if_empty;

  m_impl->DoRpc(AMQP_QUEUE_DELETE_METHOD, &del, DELETE_OK);
}

void Channel::BindQueue(const std::string& queue_name,
                        const std::string& exchange_name,
                        const std::string& routing_key)
{
  static const boost::array<uint32_t, 1> BIND_OK = { { AMQP_QUEUE_BIND_OK_METHOD } };

  amqp_queue_bind_t bind;
  bind.queue = amqp_cstring_bytes(queue_name.c_str());
  bind.exchange = amqp_cstring_bytes(exchange_name.c_str());
  bind.routing_key = amqp_cstring_bytes(routing_key.c_str());
  bind.nowait = false;
  bind.arguments = AMQP_EMPTY_TABLE;

  m_impl->DoRpc(AMQP_QUEUE_BIND_METHOD, &bind, BIND_OK);
}

void Channel::UnbindQueue(const std::string& queue_name,
                          const std::string& exchange_name,
                          const std::string& routing_key)
{
  static const boost::array<uint32_t, 1> UNBIND_OK = { { AMQP_QUEUE_UNBIND_OK_METHOD } };

  amqp_queue_unbind_t unbind;
  unbind.queue = amqp_cstring_bytes(queue_name.c_str());
  unbind.exchange = amqp_cstring_bytes(exchange_name.c_str());
  unbind.routing_key = amqp_cstring_bytes(routing_key.c_str());
  unbind.arguments = AMQP_EMPTY_TABLE;

  m_impl->DoRpc(AMQP_QUEUE_UNBIND_OK_METHOD, &unbind, UNBIND_OK);
}

void Channel::PurgeQueue(const std::string& queue_name)
{
  static const boost::array<uint32_t, 1> PURGE_OK = { { AMQP_QUEUE_PURGE_OK_METHOD } };

  amqp_queue_purge_t purge;
  purge.queue = amqp_cstring_bytes(queue_name.c_str());
  
  m_impl->DoRpc(AMQP_QUEUE_PURGE_METHOD, &purge, PURGE_OK);
}

void Channel::BasicAck(const Envelope::ptr_t& message)
{
  // Delivery tag is local to the channel, so its important to use
  // that channel, sadly this can cause the channel to throw an exception
  // which will show up as an unrelated exception in a different method
  // that actually waits for a response from the broker
  amqp_channel_t channel = message->DeliveryChannel();
  if (!m_impl->IsChannelOpen(channel))
  {
    throw std::runtime_error("The channel that the message was delivered on has been closed");
  }

	m_impl->CheckForError(amqp_basic_ack(m_impl->m_connection, channel,
    message->DeliveryTag(), false), "Channel::BasicAck basic.ack");
}

void Channel::BasicPublish(const std::string& exchange_name,
                           const std::string& routing_key,
                           const BasicMessage::ptr_t message,
                           bool mandatory,
                           bool immediate)
{
  amqp_channel_t channel = m_impl->GetChannel();

  m_impl->CheckForError(amqp_basic_publish(m_impl->m_connection, channel,
                       amqp_cstring_bytes(exchange_name.c_str()),
                       amqp_cstring_bytes(routing_key.c_str()),
                       mandatory,
                       immediate,
                       message->getAmqpProperties(),
                       message->getAmqpBody()), "Publishing to queue");

  // If we've done things correctly we can get one of 4 things back from the broker
  // - basic.ack - our channel is in confirm mode, messsage was 'dealt with' by the broker
  // - basic.return then basic.ack - the message wasn't delievered, but was dealt with
  // - channel.close - probably tried to publish to a non-existant exchange, in any case error!
  // - connection.clsoe - something really bad happened
  static const boost::array<uint32_t, 2> PUBLISH_ACK = { { AMQP_BASIC_ACK_METHOD, AMQP_BASIC_RETURN_METHOD } };
  amqp_frame_t response;
  m_impl->GetMethodOnChannel(channel, response, PUBLISH_ACK);

  if (AMQP_BASIC_RETURN_METHOD == response.payload.method.id)
  {
    MessageReturnedException message_returned = 
      m_impl->CreateMessageReturnedException(*(reinterpret_cast<amqp_basic_return_t*>(response.payload.method.decoded)), channel);

    static const boost::array<uint32_t, 1> BASIC_ACK = { { AMQP_BASIC_ACK_METHOD } };
    m_impl->GetMethodOnChannel(channel, response, BASIC_ACK);
    m_impl->ReturnChannel(channel);
    throw message_returned;
  }

  m_impl->ReturnChannel(channel);
}

bool Channel::BasicGet(Envelope::ptr_t& envelope, const std::string& queue, bool no_ack)
{
  static const boost::array<uint32_t, 2> GET_RESPONSES = { { AMQP_BASIC_GET_OK_METHOD, AMQP_BASIC_GET_EMPTY_METHOD } };

  amqp_basic_get_t get;
  get.queue = amqp_cstring_bytes(queue.c_str());
  get.no_ack = no_ack;
  
  amqp_channel_t channel = m_impl->GetChannel();
  amqp_frame_t response = m_impl->DoRpcOnChannel(channel, AMQP_BASIC_GET_METHOD, &get, GET_RESPONSES);

  if (AMQP_BASIC_GET_EMPTY_METHOD == response.payload.method.id)
  {
    m_impl->ReturnChannel(channel);
    return false;
  }

  amqp_basic_get_ok_t* get_ok = (amqp_basic_get_ok_t*)response.payload.method.decoded;
  uint64_t delivery_tag = get_ok->delivery_tag;
  bool redelivered = get_ok->redelivered;
  std::string exchange((char*)get_ok->exchange.bytes, get_ok->exchange.len);
  std::string routing_key((char*)get_ok->routing_key.bytes, get_ok->routing_key.len);

  BasicMessage::ptr_t message = m_impl->ReadContent(channel);
  envelope = Envelope::Create(message, "", delivery_tag, exchange, redelivered, routing_key, channel);

  m_impl->ReturnChannel(channel);
  return true;
}

void Channel::BasicRecover(const std::string& consumer, bool requeue)
{
  static const boost::array<uint32_t, 1> RECOVER_OK = { { AMQP_BASIC_RECOVER_OK_METHOD } };

  amqp_basic_recover_t recover;
  recover.requeue = requeue;
  
  amqp_channel_t channel = m_impl->GetConsumerChannel(consumer);

  m_impl->DoRpcOnChannel(channel, AMQP_BASIC_RECOVER_METHOD, &recover, RECOVER_OK);
}

std::string Channel::BasicConsume(const std::string& queue,
						   const std::string& consumer_tag,
						   bool no_local,
						   bool no_ack,
						   bool exclusive,
               uint16_t message_prefetch_count)
{
  amqp_channel_t channel = m_impl->GetChannel();

  // Set this before starting the consume as it may have been set by a previous consumer
  static const boost::array<uint32_t, 1> QOS_OK = { { AMQP_BASIC_QOS_OK_METHOD } };
  
  amqp_basic_qos_t qos;
  qos.prefetch_size = 0;
  qos.prefetch_count = message_prefetch_count;
  qos.global = false;

  m_impl->DoRpcOnChannel(channel, AMQP_BASIC_QOS_METHOD, &qos, QOS_OK);

  static const boost::array<uint32_t, 1> CONSUME_OK = { { AMQP_BASIC_CONSUME_OK_METHOD } };

  amqp_basic_consume_t consume;
  consume.queue = amqp_cstring_bytes(queue.c_str());
  consume.consumer_tag = amqp_cstring_bytes(consumer_tag.c_str());
  consume.no_local = no_local;
  consume.no_ack = no_ack;
  consume.exclusive = exclusive;
  consume.nowait = false;
  consume.arguments = AMQP_EMPTY_TABLE;

  amqp_frame_t response = m_impl->DoRpcOnChannel(channel, AMQP_BASIC_CONSUME_METHOD, &consume, CONSUME_OK);

  amqp_basic_consume_ok_t* consume_ok = (amqp_basic_consume_ok_t*)response.payload.method.decoded;
  std::string tag((char*)consume_ok->consumer_tag.bytes, consume_ok->consumer_tag.len);

  m_impl->AddConsumer(tag, channel);

  return tag;
}

void Channel::BasicQos(const std::string& consumer_tag, uint16_t message_prefetch_count)
{
  amqp_channel_t channel = m_impl->GetConsumerChannel(consumer_tag);

  static const boost::array<uint32_t, 1> QOS_OK = { { AMQP_BASIC_QOS_OK_METHOD } };

  amqp_basic_qos_t qos;
  qos.prefetch_size = 0;
  qos.prefetch_count = message_prefetch_count;
  qos.global = false;

  m_impl->DoRpcOnChannel(channel, AMQP_BASIC_QOS_METHOD, &qos, QOS_OK);
}

void Channel::BasicCancel(const std::string& consumer_tag)
{
  amqp_channel_t channel = m_impl->GetConsumerChannel(consumer_tag);

  static const boost::array<uint32_t, 1> CANCEL_OK = { { AMQP_BASIC_CANCEL_OK_METHOD } };

  amqp_basic_cancel_t cancel;
  cancel.consumer_tag = amqp_cstring_bytes(consumer_tag.c_str());

  m_impl->DoRpcOnChannel(channel, AMQP_BASIC_CANCEL_METHOD, &cancel, CANCEL_OK);

  m_impl->RemoveConsumer(consumer_tag);

  // Lets go hunting to make sure we don't have any queued frames lying around
  // Otherwise these frames will potentially hang around when we don't want them to
  // TODO: Implement queue purge
  m_impl->ReturnChannel(channel);
}


Envelope::ptr_t Channel::BasicConsumeMessage(const std::string& consumer_tag)
{
	Envelope::ptr_t returnval;
	BasicConsumeMessage(consumer_tag, returnval, 0);
	return returnval;
}

bool Channel::BasicConsumeMessage(const std::string& consumer_tag, Envelope::ptr_t& message, int timeout)
{
  amqp_channel_t channel = m_impl->GetConsumerChannel(consumer_tag);

  static const boost::array<uint32_t, 1> DELIVER = { { AMQP_BASIC_DELIVER_METHOD } };

  amqp_frame_t deliver;
  if (!m_impl->GetMethodOnChannel(channel, deliver, DELIVER, boost::chrono::seconds(timeout)))
  {
    return false;
  }

  amqp_basic_deliver_t* deliver_method = reinterpret_cast<amqp_basic_deliver_t*>(deliver.payload.method.decoded);

  const std::string exchange((char*)deliver_method->exchange.bytes, deliver_method->exchange.len);
  const std::string routing_key((char*)deliver_method->routing_key.bytes, deliver_method->routing_key.len);
  const std::string in_consumer_tag((char*)deliver_method->consumer_tag.bytes, deliver_method->consumer_tag.len);
  const uint64_t delivery_tag = deliver_method->delivery_tag;
  const bool redelivered = (deliver_method->redelivered == 0 ? false : true);
  
  BasicMessage::ptr_t content = m_impl->ReadContent(channel);

  message = Envelope::Create(content, in_consumer_tag, delivery_tag, exchange, redelivered, routing_key, channel);
  return true;
}

} // namespace AmqpClient
