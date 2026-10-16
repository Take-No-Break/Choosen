import 'package:choosen/core/models.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  CardItem card(String dir) => CardItem({'id': 1, 'fp': '0123456789abcdef', 'name': 'a.jpg', 'dir': dir});

  test('the first photo folder shows its folders as they are', () {
    expect(card('Artist (1)/sub').place, 'Artist (1) › sub');
    expect(card('').place, '');
  });

  test('a further photo folder is shown by its name, not its full path', () {
    expect(card(r'D:\More\Pics/Artist').place, 'Pics › Artist');
    expect(card(r'\storage\emulated\0\Pictures').place, 'Pictures');
  });
}
