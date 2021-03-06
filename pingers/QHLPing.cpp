// QTHLM
// Copyright (C) Matthew Ramir. aka "crasx" 2014
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <http://www.gnu.org/licenses/>.


#include "pingers/QHLPing.h"

QHLPing::QHLPing(QString *ip, quint16 port)
{
    this->ipAddress = ip;
    this->port = port;
    this->pingTimeoutMs = 1000;
    this->constructSocket();
    this->constructPingTimer();
    this->pingTimer = new QElapsedTimer;
    this->lastPingTime=-1;

    this->infoReply = new HL_INFO_REPLY;

}


QString *QHLPing::ToString()
{
    QString *ret=new QString();

    QTextStream(ret) << ipAddress->toLatin1()<< " " <<port;
    return ret;

}

/**
 * @brief QHLPing::testPing
 * Run a test info ping
 */
void QHLPing::executeStatusPing()
{

    QByteArray *datagram = new QByteArray();
    datagram->insert(0, "\xFF\xFF\xFF\xFFTSource Engine Query");
    datagram->append('\0');

    this->pingTimer->start();
    udpSocket->writeDatagram(*datagram, *hostAddress, port);

    if(udpSocket->waitForReadyRead(pingTimeoutMs)){
        qDebug()<<"RECV";
        this->pingMutex.lock();
        lastPingTime = pingTimer->elapsed();
        totalPingTime+=lastPingTime;
        totalPingCount++;
        this->pingMutex.unlock();


    }else{
//        qDebug()<<"Timeout";
    }

}
/**
 * @brief QHLPing::executePlayersPing
 */
void QHLPing::executePlayersPing()
{
    QByteArray *datagram = new QByteArray();
    datagram->insert(0, "\xFF\xFF\xFF\xFFU\xFF\xFF\xFF\xFF");
    datagram->append('\0');

    udpSocket->writeDatagram(*datagram, *hostAddress, port);

//    udpSocket->waitForReadyRead(this->pingTimeoutMs);



}

float QHLPing::getAveragePing()
{
    qDebug()<<"A";
    if(lastPingTime==-1)
         return 0.0;
    qDebug()<<lastPingTime;

    float r =  totalPingTime/totalPingCount;
    return r;
}

void QHLPing::pingChallengeCallback(const char *data)
{
    QByteArray *datagram = new QByteArray();
    datagram->insert(0, "\xFF\xFF\xFF\xFFU");
    datagram->insert(5, data);
    datagram->append('\0');

    udpSocket->writeDatagram(*datagram, *hostAddress, port);


//    udpSocket->waitForReadyRead(this->pingTimeoutMs);


}
/**
 * @brief QHLPing::processPendingDatagrams
 * Data recieved callback
 */
void QHLPing::processPendingDatagrams()
{
    QByteArray datagram;


    do {
        datagram.resize(udpSocket->pendingDatagramSize());
        udpSocket->readDatagram(datagram.data(), datagram.size());
    } while (udpSocket->hasPendingDatagrams());

    //process returned data
    processPing(datagram.constData(), datagram.count());



}

void QHLPing::timerCallback()
{
    this->executeStatusPing();
//    this->executePlayersPing();


}

/**
 * @brief QHLPing::constructSocket
 * Creates new socket and adds listeners
 */
void QHLPing::constructSocket()
{
    hostAddress = new QHostAddress(*this->ipAddress);

    udpSocket = new QUdpSocket();

    //add event listener
    connect(udpSocket, SIGNAL(readyRead()), this, SLOT(processPendingDatagrams()));



}

/**
 * @brief QHLPing::constructPingTimer
 * creates ping timer to be used in continuious pinging
 */
void QHLPing::constructPingTimer()
{
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timerCallback()));
    timer->start(this->pingTimeoutMs);


}

/**
 * @brief QHLPing::processPing
 * Processes ping responses based on spec at https://developer.valvesoftware.com/wiki/Server_queries
 * @param data
 * @param len
 */
void QHLPing::processPing(const char *data, int len)
{

    char type='\0';
    const char * dataPtr ;

    if(len>5){ // grab header at byte 5, first 4 are 0xff
        type = data[4];
    }

    switch(type){
        case 'I': //info ping

            infoReplyMutex.lock();

            if(len>6)
                this->infoReply->protocol = data[5];

            //start copying data over c style
            dataPtr= data+6;

//            processPingGrabString(this->infoReply->hostname, &dataPtr, len, data-dataPtr);
            this->infoReply->hostname = dataPtr;
            dataPtr+=this->infoReply->hostname.length()+1;

            this->infoReply->map = dataPtr;
            dataPtr+=this->infoReply->map.length()+1;

            this->infoReply->game_directory= dataPtr;
            dataPtr+=this->infoReply->game_directory.length()+1;

            this->infoReply->game_description= dataPtr;
            dataPtr+=this->infoReply->game_description.length()+1;


            this->infoReply->app_id = (unsigned short)
                    ((unsigned char)dataPtr[1]) << 8 |
                    ((unsigned char)dataPtr[0]);

            this->infoReply->num_players = dataPtr[2];
            this->infoReply->max_players = dataPtr[3];
            this->infoReply->num_of_bots = dataPtr[4];
            this->infoReply->is_private = dataPtr[5];
            this->infoReply->os = dataPtr[6];
            this->infoReply->password = dataPtr[7];
            this->infoReply->secure = dataPtr[8];

            dataPtr +=9;

            if(this->infoReply->game_id == 2400){ // "the ship"
                this->infoReply->ship_mode=dataPtr[0];
                this->infoReply->ship_whitneses=dataPtr[1];
                this->infoReply->ship_duration=dataPtr[2];
                dataPtr+=3;
            }

            this->infoReply->game_version= dataPtr;
            dataPtr+=this->infoReply->game_version.length();

            this->infoReply->extra_data = dataPtr[0];
            dataPtr++;

            if(this->infoReply->extra_data & 0x80){ //extra data flag  - port
                this->infoReply->port = (unsigned short)
                        ((unsigned char)dataPtr[1]) << 8 |
                        ((unsigned char)dataPtr[0]);
                dataPtr+=2;
            }

            if(this->infoReply->extra_data & 0x10){ //extra data flag - steam id - 64 bits
                this->infoReply->steamid = 0;
                for(int i=0;i<8;i++){
                    this->infoReply->steamid = this->infoReply->steamid | (((unsigned char)dataPtr[i]) << 8*i);
                }

                dataPtr+=8;

            }

            if(this->infoReply->extra_data & 0x40){ //extra data flag - sourcetv, short and string
                this->infoReply->sourcetv_port = (unsigned short)
                        ((unsigned char)dataPtr[1]) << 8 |
                        ((unsigned char)dataPtr[0]);
                dataPtr+=2;

                this->infoReply->sourcetv_name= dataPtr;
                dataPtr+=this->infoReply->sourcetv_name.length()+1;



            }

            if(this->infoReply->extra_data & 0x20){ //extra data flag - keywords
                this->infoReply->keywords= dataPtr;
                dataPtr+=this->infoReply->keywords.length()+1;


            }

            if(this->infoReply->extra_data & 0x01){ //extra data flag - gameid, long long
                this->infoReply->game_id = 0;
                for(int i=0;i<8;i++){
                    this->infoReply->game_id = this->infoReply->game_id | (((unsigned char)dataPtr[i]) << 8*i);
                }

                dataPtr+=8;

            }

            infoReplyMutex.unlock();

        break;
    case 'A': //player challenge
        qDebug()<<len;
        if(len==9){
            pingChallengeCallback(data+5);
        }
        break;
     case 'D':
        // This is the response after a successful challenge response (A2S_PLAYER)

        //TODO: mutex

        if(len<6)break; // should be at least 6 bytes, header+num players

        unsigned short players=data[5];//number of players gotten

        dataPtr=data+6; //move pointer to first player

        if(players>128) players=128; // prevent buffer overflow, i highly doubt any game has more than 128 players, we can always (should) TODO: make this a preprocessor var

        for(int i=0;i<players;i++){

            //process Data

            this->playerInfo[i].index=dataPtr[0];
            dataPtr++;

            if(dataPtr>=data+len) //prevent buffer overfl0w
                break;



            this->playerInfo[i].name =dataPtr;
            dataPtr+=this->playerInfo[i].name.length()+1;



            this->playerInfo[i].score = *(long*)&dataPtr[0];
            dataPtr+=4;
            this->playerInfo[i].time = *(float*)&dataPtr[0];
            dataPtr +=4;


            qDebug()<<  this->playerInfo[i].index ;
             qDebug()<<this->playerInfo[i].name ;
             qDebug()<<this->playerInfo[i].score ;
            qDebug()<<  this->playerInfo[i].time;



        }

        break;

    }

}


