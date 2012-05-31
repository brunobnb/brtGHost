/*

   Copyright [2008] [Trevor Hogan]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

#ifdef WIN32
 #include "ms_stdint.h"
#else
 #include <stdint.h>
#endif

#include "elo.h"

#include <string.h>

#ifdef WIN32
 #include <winsock2.h>
#endif

#include <mysql/mysql.h>
#include <algorithm>

#include "includes.h"
#include "util.h"

vector<string> MySQLFetchRow( MYSQL_RES *res );
string MySQLEscapeString( MYSQL *conn, string str );

float UTIL_ToFloat( string &s )
{
	float result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

bool updateDotaElo( MYSQL *Connection )
{
	/*
	string CFGFile = "update_dota_elo.cfg";

	if( argc > 1 && argv[1] )
		CFGFile = argv[1];

	CConfig CFG;
	CFG.Read( CFGFile );
	string Server = CFG.GetString( "db_mysql_server", string( ) );
	string Database = CFG.GetString( "db_mysql_database", "ghost" );
	string User = CFG.GetString( "db_mysql_user", string( ) );
	string Password = CFG.GetString( "db_mysql_password", string( ) );
	int Port = CFG.GetInt( "db_mysql_port", 0 );
*/
	bool DontUpdateScoresToAdmins = false; //CFG.GetInt("elo_dont_calculate_score_to_admins", 0) == 1 ? true : false;

	vector<string> AdminsList; AdminsList.clear();
/*
	cout << "connecting to database server" << endl;
	MYSQL *Connection = NULL;

	if( !( Connection = mysql_init( NULL ) ) )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	my_bool Reconnect = true;
	mysql_options( Connection, MYSQL_OPT_RECONNECT, &Reconnect );

	if( !( mysql_real_connect( Connection, Server.c_str( ), User.c_str( ), Password.c_str( ), Database.c_str( ), Port, NULL, 0 ) ) )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}
*/
	cout << "Checking Admins List" << endl;

	string QAdminsList = "SELECT name FROM admins;";

	if( mysql_real_query( Connection, QAdminsList.c_str( ), QAdminsList.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}
	else
	{
		MYSQL_RES *Result = mysql_store_result( Connection );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			while( !Row.empty( ) )
			{
				string name = Row[0];
				transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );

				AdminsList.push_back( name );
				Row = MySQLFetchRow( Result );
			}

			mysql_free_result( Result );
		}
		else
		{
			cout << "error: " << mysql_error( Connection ) << endl;
			return 1;
		}
	}

	cout << "beginning transaction" << endl;

	string QBegin = "BEGIN";

	if( mysql_real_query( Connection, QBegin.c_str( ), QBegin.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	cout << "creating tables" << endl;

	string QCreate1 = "CREATE TABLE IF NOT EXISTS dota_elo_scores ( id INT NOT NULL AUTO_INCREMENT PRIMARY KEY, name VARCHAR(15) NOT NULL, server VARCHAR(100) NOT NULL, score REAL NOT NULL )";
	string QCreate2 = "CREATE TABLE IF NOT EXISTS dota_elo_games_scored ( id INT NOT NULL AUTO_INCREMENT PRIMARY KEY, gameid INT NOT NULL )";

	if( mysql_real_query( Connection, QCreate1.c_str( ), QCreate1.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	if( mysql_real_query( Connection, QCreate2.c_str( ), QCreate2.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	cout << "getting unscored games" << endl;
	queue<uint32_t> UnscoredGames;

	string QSelectUnscored = "SELECT id FROM games WHERE id NOT IN ( SELECT gameid FROM dota_elo_games_scored ) ORDER BY id";

	if( mysql_real_query( Connection, QSelectUnscored.c_str( ), QSelectUnscored.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}
	else
	{
		MYSQL_RES *Result = mysql_store_result( Connection );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			while( !Row.empty( ) )
			{
				UnscoredGames.push( UTIL_ToUInt32( Row[0] ) );
				Row = MySQLFetchRow( Result );
			}

			mysql_free_result( Result );
		}
		else
		{
			cout << "error: " << mysql_error( Connection ) << endl;
			return 1;
		}
	}

	cout << "found " << UnscoredGames.size( ) << " unscored games" << endl;

	while( !UnscoredGames.empty( ) )
	{
		uint32_t GameID = UnscoredGames.front( );
		UnscoredGames.pop( );

		string QSelectPlayers = "SELECT dota_elo_scores.id, gameplayers.name, spoofedrealm, newcolour, winner, score FROM dotaplayers LEFT JOIN dotagames ON dotagames.gameid=dotaplayers.gameid LEFT JOIN gameplayers ON gameplayers.gameid=dotaplayers.gameid AND gameplayers.colour=dotaplayers.colour LEFT JOIN dota_elo_scores ON dota_elo_scores.name=gameplayers.name WHERE dotaplayers.gameid=" + UTIL_ToString( GameID );

		if( mysql_real_query( Connection, QSelectPlayers.c_str( ), QSelectPlayers.size( ) ) != 0 )
		{
			cout << "error: " << mysql_error( Connection ) << endl;
			return 1;
		}
		else
		{
			MYSQL_RES *Result = mysql_store_result( Connection );

			if( Result )
			{
				cout << "gameid " << UTIL_ToString( GameID ) << " found" << endl;

				bool ignore = false;
				uint32_t rowids[10];
				string names[10];
				string servers[10];
				bool exists[10];
				int num_players = 0;
				float player_ratings[10];
				int player_teams[10];
				int num_teams = 2;
				float team_ratings[2];
				float team_winners[2];
				int team_numplayers[2];
				team_ratings[0] = 0.0;
				team_ratings[1] = 0.0;
				team_numplayers[0] = 0;
				team_numplayers[1] = 0;

				vector<string> Row = MySQLFetchRow( Result );

				while( Row.size( ) == 6 )
				{
					if( num_players >= 10 )
					{
						cout << "gameid " << UTIL_ToString( GameID ) << " has more than 10 players, ignoring" << endl;
						ignore = true;
						break;
					}

					uint32_t Winner = UTIL_ToUInt32( Row[4] );

					if( Winner != 1 && Winner != 2 )
					{
						cout << "gameid " << UTIL_ToString( GameID ) << " has no winner, ignoring" << endl;
						ignore = true;
						break;
					}
					else if( Winner == 1 )
					{
						team_winners[0] = 1.0;
						team_winners[1] = 0.0;
					}
					else
					{
						team_winners[0] = 0.0;
						team_winners[1] = 1.0;
					}

					if( !Row[0].empty( ) )
						rowids[num_players] = UTIL_ToUInt32( Row[0] );
					else
						rowids[num_players] = 0;

					names[num_players] = Row[1];

					transform( names[num_players].begin( ), names[num_players].end( ), names[num_players].begin( ), (int(*)(int))tolower );

					servers[num_players] = Row[2];

					if( !Row[5].empty( ) )
					{
						exists[num_players] = true;
						player_ratings[num_players] = UTIL_ToFloat( Row[5] );
					}
					else
					{
						cout << "new player [" << Row[1] << "] found" << endl;
						exists[num_players] = false;
						player_ratings[num_players] = 1000.0;
					}

					uint32_t Colour = UTIL_ToUInt32( Row[3] );

					if( Colour >= 1 && Colour <= 5 )
					{
						player_teams[num_players] = 0;
						team_ratings[0] += player_ratings[num_players];
						team_numplayers[0]++;
					}
					else if( Colour >= 7 && Colour <= 11 )
					{
						player_teams[num_players] = 1;
						team_ratings[1] += player_ratings[num_players];
						team_numplayers[1]++;
					}
					else
					{
						cout << "gameid " << UTIL_ToString( GameID ) << " has a player with an invalid newcolour, ignoring" << endl;
						ignore = true;
						break;
					}

					num_players++;
					Row = MySQLFetchRow( Result );
				}

				mysql_free_result( Result );

				if( !ignore )
				{
					if( num_players == 0 )
						cout << "gameid " << UTIL_ToString( GameID ) << " has no players, ignoring" << endl;
					else if( team_numplayers[0] == 0 )
						cout << "gameid " << UTIL_ToString( GameID ) << " has no Sentinel players, ignoring" << endl;
					else if( team_numplayers[1] == 0 )
						cout << "gameid " << UTIL_ToString( GameID ) << " has no Scourge players, ignoring" << endl;
					else
					{
						cout << "gameid " << UTIL_ToString( GameID ) << " is calculating" << endl;

						float old_player_ratings[10];
						memcpy( old_player_ratings, player_ratings, sizeof( float ) * 10 );
						team_ratings[0] /= team_numplayers[0];
						team_ratings[1] /= team_numplayers[1];
						elo_recalculate_ratings( num_players, player_ratings, player_teams, num_teams, team_ratings, team_winners );

						string QNullEloPoint = "UPDATE dotaplayers SET elopoint=0 WHERE gameid=" + UTIL_ToString( GameID );

						if( mysql_real_query( Connection, QNullEloPoint.c_str( ), QNullEloPoint.size( ) ) != 0 )
                        {
                            cout << "error: " << mysql_error( Connection ) << endl;
                            return 1;
                        }

						vector<string> player_who_scored;
						player_who_scored.clear();

                        string QSelectPlayerPoint = "SELECT dp.id,gp.name FROM dotaplayers as dp LEFT JOIN gameplayers as gp ON gp.gameid=dp.gameid AND gp.colour=dp.colour LEFT JOIN dotagames as dg ON dg.gameid=dp.gameid LEFT JOIN games ON games.id=dp.gameid WHERE (gp.left > games.duration - 60 OR (gp.left < games.duration - 60 AND ((dg.winner=1 AND dp.newcolour>5) OR (dg.winner=2 AND dp.newcolour<6)))) AND dp.gameid="+UTIL_ToString( GameID );

                        if( mysql_real_query( Connection, QSelectPlayerPoint.c_str( ), QSelectPlayerPoint.size( ) ) != 0 )
                        {
                            cout << "error: " << mysql_error( Connection ) << endl;
                            return 1;
                        } 
						else
                        {

                            MYSQL_RES *Result = mysql_store_result( Connection );

                            if( Result )
                            {
                                vector<string> Row = MySQLFetchRow( Result );

                                while( !Row.empty( ) )
                                {
                                        string elopoint; elopoint = "0.0";
                                        int player_id = -1;
										string cur_name; cur_name = Row[1];
										
										transform( cur_name.begin( ), cur_name.end( ), cur_name.begin( ), (int(*)(int))tolower );
	
                                        for (player_id = 0;player_id < 10; player_id++)
                                        if (names[player_id] == cur_name) break;

										if (player_id < 0)
										{
											cout << "[ELO] Error, player_id < 0" << endl;
											break;
										}

										if (DontUpdateScoresToAdmins)
										{
												for ( vector<string> :: iterator i = AdminsList.begin(); i != AdminsList.end(); i++)
													if ( (*i) == names[player_id] )
												{
													player_ratings[player_id] = old_player_ratings[player_id];
													break;
												}

										}

										elopoint = UTIL_ToString( (double)(player_ratings[player_id] - old_player_ratings[player_id]) );

                                        string QUpdatePlayerPoint = "UPDATE dotaplayers SET elopoint="+elopoint+" WHERE gameid="+UTIL_ToString( GameID ) + " AND id=" + Row[0];

                                        if( mysql_real_query( Connection, QUpdatePlayerPoint.c_str( ), QUpdatePlayerPoint.size( ) ) != 0 )
                                        {
                                            cout << "error: " << mysql_error( Connection ) << endl;
                                            return 1;
                                        }

										if( exists[player_id] )
										{
											string QUpdateScore = "UPDATE dota_elo_scores SET score=" + UTIL_ToString( player_ratings[player_id], 2 ) + " WHERE id=" + UTIL_ToString( rowids[player_id] );

											if( mysql_real_query( Connection, QUpdateScore.c_str( ), QUpdateScore.size( ) ) != 0 )
											{
												cout << "error: " << mysql_error( Connection ) << endl;
												return 1;
											}
										}
										else
										{
											string EscName = MySQLEscapeString( Connection, names[player_id] );
											string EscServer = MySQLEscapeString( Connection, servers[player_id] );

											string QInsertScore = "INSERT INTO dota_elo_scores ( name, server, score ) VALUES ( '" + EscName + "', '" + EscServer + "', " + UTIL_ToString( player_ratings[player_id], 2 ) + " )";

											if(mysql_real_query( Connection, QInsertScore.c_str( ), QInsertScore.size( ) ) != 0 )
											{
												cout << "error: " << mysql_error( Connection ) << endl;
												return 1;
											}
										}

                                    player_who_scored.push_back(Row[1]);
                                    Row = MySQLFetchRow( Result );
                                }

                            }

                            mysql_free_result( Result );
                        }

						for( int i = 0; i < num_players; i++ )
						{
						    bool id_rating;
						    id_rating = false;

						    for (uint32_t cid=0;cid < player_who_scored.size(); cid++)
                                if (player_who_scored[cid] == names[i])
                                {
                                    id_rating = true;
                                    break;
                                }

                            if (!id_rating)
                                old_player_ratings[i] = player_ratings[i];

							cout << "player [" << names[i] << "] rating " << UTIL_ToString( (uint32_t)old_player_ratings[i] ) << " -> " << UTIL_ToString( (uint32_t)player_ratings[i] ) << endl;

						}
					}
				}
			}
			else
			{
				cout << "error: " << mysql_error( Connection ) << endl;
				return 1;
			}
		}

		string QInsertScored = "INSERT INTO dota_elo_games_scored ( gameid ) VALUES ( " + UTIL_ToString( GameID ) + " )";

		if( mysql_real_query( Connection, QInsertScored.c_str( ), QInsertScored.size( ) ) != 0 )
		{
			cout << "error: " << mysql_error( Connection ) << endl;
			return 1;
		}
	}

	cout << "copying dota elo scores to scores table" << endl;

	string QCopyScores1 = "DELETE FROM scores WHERE category='dota_elo'";
	string QCopyScores2 = "INSERT INTO scores ( category, name, server, score ) SELECT 'dota_elo', name, server, score FROM dota_elo_scores";

	if( mysql_real_query( Connection, QCopyScores1.c_str( ), QCopyScores1.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	if( mysql_real_query( Connection, QCopyScores2.c_str( ), QCopyScores2.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	cout << "committing transaction" << endl;

	string QCommit = "COMMIT";

	if( mysql_real_query( Connection, QCommit.c_str( ), QCommit.size( ) ) != 0 )
	{
		cout << "error: " << mysql_error( Connection ) << endl;
		return 1;
	}

	cout << "done" << endl;
	return 0;
}
