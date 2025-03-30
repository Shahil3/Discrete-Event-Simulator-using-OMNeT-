//
// Generated file, do not edit! Created by opp_msgtool 6.1 from messages.msg.
//

#ifndef __MESSAGES_M_H
#define __MESSAGES_M_H

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <omnetpp.h>

// opp_msgtool version check
#define MSGC_VERSION 0x0601
#if (MSGC_VERSION!=OMNETPP_VERSION)
#    error Version mismatch! Probably this file was generated by an earlier version of opp_msgtool: 'make clean' should help.
#endif

class SubtaskMessage;
class ResultMessage;
class GossipMessage;
/**
 * Class generated from <tt>messages.msg:15</tt> by opp_msgtool.
 * <pre>
 * //
 * // This program is free software: you can redistribute it and/or modify
 * // it under the terms of the GNU Lesser General Public License as published by
 * // the Free Software Foundation, either version 3 of the License, or
 * // (at your option) any later version.
 * // 
 * // This program is distributed in the hope that it will be useful,
 * // but WITHOUT ANY WARRANTY; without even the implied warranty of
 * // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * // GNU Lesser General Public License for more details.
 * // 
 * // You should have received a copy of the GNU Lesser General Public License
 * // along with this program.  If not, see http://www.gnu.org/licenses/.
 * //
 * message SubtaskMessage
 * {
 *     int clientId;
 *     int subtaskId;
 *     int data[];
 * }
 * </pre>
 */
class SubtaskMessage : public ::omnetpp::cMessage
{
  protected:
    int clientId = 0;
    int subtaskId = 0;
    int *data = nullptr;
    size_t data_arraysize = 0;

  private:
    void copy(const SubtaskMessage& other);

  protected:
    bool operator==(const SubtaskMessage&) = delete;

  public:
    SubtaskMessage(const char *name=nullptr, short kind=0);
    SubtaskMessage(const SubtaskMessage& other);
    virtual ~SubtaskMessage();
    SubtaskMessage& operator=(const SubtaskMessage& other);
    virtual SubtaskMessage *dup() const override {return new SubtaskMessage(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    virtual int getClientId() const;
    virtual void setClientId(int clientId);

    virtual int getSubtaskId() const;
    virtual void setSubtaskId(int subtaskId);

    virtual void setDataArraySize(size_t size);
    virtual size_t getDataArraySize() const;
    virtual int getData(size_t k) const;
    virtual void setData(size_t k, int data);
    virtual void insertData(size_t k, int data);
    [[deprecated]] void insertData(int data) {appendData(data);}
    virtual void appendData(int data);
    virtual void eraseData(size_t k);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const SubtaskMessage& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, SubtaskMessage& obj) {obj.parsimUnpack(b);}

/**
 * Class generated from <tt>messages.msg:21</tt> by opp_msgtool.
 * <pre>
 * message ResultMessage
 * {
 *     int clientId;
 *     int subtaskId;
 *     int result;
 * }
 * </pre>
 */
class ResultMessage : public ::omnetpp::cMessage
{
  protected:
    int clientId = 0;
    int subtaskId = 0;
    int result = 0;

  private:
    void copy(const ResultMessage& other);

  protected:
    bool operator==(const ResultMessage&) = delete;

  public:
    ResultMessage(const char *name=nullptr, short kind=0);
    ResultMessage(const ResultMessage& other);
    virtual ~ResultMessage();
    ResultMessage& operator=(const ResultMessage& other);
    virtual ResultMessage *dup() const override {return new ResultMessage(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    virtual int getClientId() const;
    virtual void setClientId(int clientId);

    virtual int getSubtaskId() const;
    virtual void setSubtaskId(int subtaskId);

    virtual int getResult() const;
    virtual void setResult(int result);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const ResultMessage& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, ResultMessage& obj) {obj.parsimUnpack(b);}

/**
 * Class generated from <tt>messages.msg:27</tt> by opp_msgtool.
 * <pre>
 * message GossipMessage
 * {
 *     string senderId;
 *     string content; // format: <timestamp>:<clientId>:<serverScore#>
 * }
 * </pre>
 */
class GossipMessage : public ::omnetpp::cMessage
{
  protected:
    omnetpp::opp_string senderId;
    omnetpp::opp_string content;

  private:
    void copy(const GossipMessage& other);

  protected:
    bool operator==(const GossipMessage&) = delete;

  public:
    GossipMessage(const char *name=nullptr, short kind=0);
    GossipMessage(const GossipMessage& other);
    virtual ~GossipMessage();
    GossipMessage& operator=(const GossipMessage& other);
    virtual GossipMessage *dup() const override {return new GossipMessage(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    virtual const char * getSenderId() const;
    virtual void setSenderId(const char * senderId);

    virtual const char * getContent() const;
    virtual void setContent(const char * content);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const GossipMessage& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, GossipMessage& obj) {obj.parsimUnpack(b);}


namespace omnetpp {

template<> inline SubtaskMessage *fromAnyPtr(any_ptr ptr) { return check_and_cast<SubtaskMessage*>(ptr.get<cObject>()); }
template<> inline ResultMessage *fromAnyPtr(any_ptr ptr) { return check_and_cast<ResultMessage*>(ptr.get<cObject>()); }
template<> inline GossipMessage *fromAnyPtr(any_ptr ptr) { return check_and_cast<GossipMessage*>(ptr.get<cObject>()); }

}  // namespace omnetpp

#endif // ifndef __MESSAGES_M_H

